#include "hypervisor.h"
#include "file_manager.h"
#include "io_handler.h"
#include "irq.h"
#include "serial.h"
#include "shared_buffer.h"
#include "shared_files.h"

#include <errno.h>
#include <linux/kvm.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#define SBUF_DATA_PORT 0x0510
#define SBUF_CTRL_PORT 0x0520

enum sbuf_phase {
  SBUF_PHASE_IDLE,
  SBUF_PHASE_WRITER_COUNT,
  SBUF_PHASE_WRITER_DATA,
  SBUF_PHASE_WRITER_DONE,
  SBUF_PHASE_READER_COUNT,
  SBUF_PHASE_READER_DATA,
  SBUF_PHASE_READER_DONE,
};

struct sbuf_proto_state {
  enum sbuf_phase phase;
  char buf[BUFFER_SIZE];
  uint32_t count;
  uint32_t idx;
  uint32_t byte_idx;
  uint32_t actual;
};

static struct sbuf_proto_state sbuf_states[MAX_GUESTS];

static int sbuf_data_handler(struct vm_context *ctx, int direction, uint16_t port, void *data, uint32_t size) {
  (void)port;
  (void)size;

  struct sbuf_proto_state *ps = &sbuf_states[ctx->vm_id];

  if (!ctx->role_assigned) {
    if (direction == KVM_EXIT_IO_IN) {
      *(uint8_t *)data = (uint8_t)ctx->role;
      ctx->role_assigned = 1;
      ps->phase = SBUF_PHASE_IDLE;
    }
    return 0;
  }

  if (ctx->role == ROLE_WRITER && direction == KVM_EXIT_IO_OUT) {
    switch (ps->phase) {
      case SBUF_PHASE_IDLE:
        ps->count = 0;
        ps->byte_idx = 0;
        ps->idx = 0;
        ps->phase = SBUF_PHASE_WRITER_COUNT;
      case SBUF_PHASE_WRITER_COUNT:
        ps->count |= ((uint32_t)(*(uint8_t *)data)) << (8 * ps->byte_idx);
        ps->byte_idx++;
        if (ps->byte_idx >= 4) {
          if (ps->count > BUFFER_SIZE) {
            ps->count = BUFFER_SIZE;
          }
          ps->phase = (ps->count > 0) ? SBUF_PHASE_WRITER_DATA : SBUF_PHASE_WRITER_DONE;
        }
        break;

      case SBUF_PHASE_WRITER_DATA:
        if (ps->idx < ps->count) {
          ps->buf[ps->idx] = *(char *)data;
        }
        ps->idx++;
        if (ps->idx >= ps->count) {
          ps->actual = sbuf_write(&ctx->hv->sbuf, ps->buf, ps->count);
          sbuf_notify_readers(&ctx->hv->sbuf);
          ps->phase = SBUF_PHASE_WRITER_DONE;
        }
        break;

      default:
        break;
      }
    return 0;
  }

  if (ctx->role == ROLE_READER && direction == KVM_EXIT_IO_IN) {
    switch (ps->phase) {
      case SBUF_PHASE_IDLE:
        sbuf_reader_wait(&ctx->hv->sbuf, ps->buf, &ps->count);
        ps->byte_idx = 0;
        ps->idx = 0;
        ps->phase = SBUF_PHASE_READER_COUNT;
      case SBUF_PHASE_READER_COUNT:
        *(uint8_t *)data = (uint8_t)(ps->count >> (8 * ps->byte_idx));
        ps->byte_idx++;
        if (ps->byte_idx >= 4) {
          ps->phase = (ps->count > 0) ? SBUF_PHASE_READER_DATA : SBUF_PHASE_READER_DONE;
        }
        break;

      case SBUF_PHASE_READER_DATA:
        *(uint8_t *)data = (uint8_t)ps->buf[ps->idx++];
        if (ps->idx >= ps->count) {
          ps->actual = ps->count;
          ps->phase = SBUF_PHASE_READER_DONE;
        }
        break;

      default:
        *(uint8_t *)data = 0;
        break;
    }
    return 0;
  }

  return 0;
}

static int sbuf_ctrl_handler(struct vm_context *ctx, int direction, uint16_t port, void *data, uint32_t size) {
  (void)port;
  (void)size;

  struct sbuf_proto_state *ps = &sbuf_states[ctx->vm_id];

  if (ctx->role == ROLE_WRITER && direction == KVM_EXIT_IO_IN) {
    if (ps->phase == SBUF_PHASE_WRITER_DONE) {
      for (int i = 0; i < ctx->hv->vm_count; i++) {
        if (ctx->hv->vms[i].role == ROLE_READER) {
          irq_queue_push(&ctx->hv->vms[i].irq_q, 32);
          pthread_kill(ctx->hv->threads[i], SIGUSR1);
        }
      }

      sbuf_wait_all_readers(&ctx->hv->sbuf);
      *(uint32_t *)data = ps->actual;
      ps->phase = SBUF_PHASE_IDLE;
    } else {
      *(uint32_t *)data = 0;
    }
    return 0;
  }

  if (ctx->role == ROLE_READER && direction == KVM_EXIT_IO_OUT) {
    uint32_t bytes_read = *(uint32_t *)data;
    (void)bytes_read;
    sbuf_reader_done(&ctx->hv->sbuf);
    ps->phase = SBUF_PHASE_IDLE;
    return 0;
  }

  return 0;
}

static void *vm_thread_fn(void *arg) {
  struct vm_context *ctx = (struct vm_context *)arg;
  struct vm *v = &ctx->vm;
  struct kvm_sregs sregs;
  struct kvm_regs regs;
  int ret;

  if (vm_init(v, ctx->hv->cfg->mem_size, ctx->hv->cfg->page_size) < 0) {
    fprintf(stderr, "[VM %d] Failed to init VM\n", ctx->vm_id);
    return NULL;
  }

  if (ioctl(v->vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
    perror("KVM_GET_SREGS");
    vm_destroy(v);
    return NULL;
  }

  setup_long_mode(v, &sregs);

  if (ioctl(v->vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
    perror("KVM_SET_SREGS");
    vm_destroy(v);
    return NULL;
  }

  if (load_guest_image(v, ctx->image_path, GUEST_START_ADDR) < 0) {
    fprintf(stderr, "[VM %d] Failed to load guest image: %s\n", ctx->vm_id, ctx->image_path);
    vm_destroy(v);
    return NULL;
  }

  memset(&regs, 0, sizeof(regs));
  regs.rflags = 0x2;
  regs.rip = GUEST_START_ADDR;
  regs.rsp = 2 << 20; // dno steka koji raste na gore na vrhu memorije

  if (ioctl(v->vcpu_fd, KVM_SET_REGS, &regs) < 0) {
    perror("KVM_SET_REGS");
    vm_destroy(v);
    return NULL;
  }

  if (ctx->role != ROLE_NONE) {
    irq_queue_push(&ctx->irq_q, 32);
  }

  printf("[VM %d] Starting execution (role=%d)\n", ctx->vm_id, ctx->role);

  while (!atomic_load(&ctx->hv->shutdown_flag)) {
    if (irq_queue_pending(&ctx->irq_q)) {
      irq_request_window(v);
    }

    ret = ioctl(v->vcpu_fd, KVM_RUN, 0);
    if (ret == -1) {
      if (errno == EINTR) continue;
      fprintf(stderr, "[VM %d] KVM_RUN failed\n", ctx->vm_id);
      break;
    }

    switch (v->run->exit_reason) {
      case KVM_EXIT_IO:
        if (io_dispatch(ctx, v->run) < 0) {
          fprintf(stderr, "[VM %d] IO handler error\n", ctx->vm_id);
          goto exit_loop;
        }
        continue;

      case KVM_EXIT_HLT:
        printf("[VM %d] HLT – VM stopped\n", ctx->vm_id);
        goto exit_loop;

      case KVM_EXIT_SHUTDOWN:
        fprintf(stderr, "[VM %d] Shutdown (triple fault)\n", ctx->vm_id);
        goto exit_loop;

      case KVM_EXIT_IRQ_WINDOW_OPEN:
        if (irq_handle_window(ctx) < 0) {
          fprintf(stderr, "[VM %d] IRQ injection error\n", ctx->vm_id);
          goto exit_loop;
        }
        continue;

      default:
        fprintf(stderr, "[VM %d] Unexpected exit reason: %d\n", ctx->vm_id, v->run->exit_reason);
        goto exit_loop;
    }
  }

exit_loop:
  printf("[VM %d] Thread exiting\n", ctx->vm_id);
  file_manager_destroy(&ctx->fm);
  vm_destroy(v);
  return NULL;
}

int hypervisor_init(struct hypervisor *hv, struct config *cfg) {
  memset(hv, 0, sizeof(*hv));
  hv->cfg = cfg;
  hv->vm_count = cfg->guest_count;
  atomic_store(&hv->shutdown_flag, 0);

  shared_files_init(&hv->sfr, cfg);

  serial_register_handlers();
  file_io_register_handlers();
  io_register_handler(SBUF_DATA_PORT, sbuf_data_handler);
  io_register_handler(SBUF_CTRL_PORT, sbuf_ctrl_handler);

  hv->writer_vm_id = -1;
  int reader_count = 0;

  if (hv->vm_count >= 2) {
    hv->writer_vm_id = 0; // prva masina zadata preko cli postaje pisac
    reader_count = hv->vm_count - 1;
  }

  sbuf_init(&hv->sbuf, reader_count);

  memset(sbuf_states, 0, sizeof(sbuf_states));

  for (int i = 0; i < hv->vm_count; i++) {
    hv->vms[i].vm_id = i;
    hv->vms[i].image_path = cfg->guest_images[i];
    hv->vms[i].hv = hv;
    hv->vms[i].role_assigned = 0;

    if (hv->vm_count >= 2) {
      hv->vms[i].role = (i == hv->writer_vm_id) ? ROLE_WRITER : ROLE_READER;
    } else {
      hv->vms[i].role = ROLE_NONE;
    }

    file_manager_init(&hv->vms[i].fm, i);
    irq_queue_init(&hv->vms[i].irq_q);
  }

  return 0;
}

int hypervisor_run(struct hypervisor *hv) {
  int errors = 0;

  for (int i = 0; i < hv->vm_count; i++) {
    int rc = pthread_create(&hv->threads[i], NULL, vm_thread_fn, &hv->vms[i]);
    if (rc != 0) {
      fprintf(stderr, "[VM %d] Failed to create thread: %d\n", i, rc);
      errors++;
    }
  }

  for (int i = 0; i < hv->vm_count; i++) {
    pthread_join(hv->threads[i], NULL);
  }

  if (errors > 0) {
    fprintf(stderr, "Hypervisor: %d thread(s) failed to start\n", errors);
    return -1;
  }

  return 0;
}

void hypervisor_shutdown(struct hypervisor *hv) {
  atomic_store(&hv->shutdown_flag, 1);
}

void hypervisor_destroy(struct hypervisor *hv) { sbuf_destroy(&hv->sbuf); }
