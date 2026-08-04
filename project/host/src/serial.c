#include "serial.h"
#include "hypervisor.h"
#include "io_handler.h"
#include "irq.h"

#include <linux/kvm.h>
#include <signal.h>
#include <stdio.h>

#define MAX_VMS 16
#define BUF_SIZE 256
static char print_buf[MAX_VMS][BUF_SIZE];
static int print_len[MAX_VMS];

static int serial_handler(struct vm_context *ctx, int direction, uint16_t port, void *data, uint32_t size) {
  (void)port;
  (void)size;

  if (direction == KVM_EXIT_IO_OUT) { // guest -> stdout
    char c = *(char *)data;
    int id = ctx->vm_id;

    if (id >= 0 && id < MAX_VMS) {
      if (c == '\n' || print_len[id] >= BUF_SIZE - 1) {
        print_buf[id][print_len[id]] = '\0';
        printf("[VM %d] %s\n", id, print_buf[id]);
        print_len[id] = 0;
      } else {
        print_buf[id][print_len[id]++] = c;
      }
    } else {
      putchar(c);
      fflush(stdout);
    }
  } else { // stdin -> guest
    int c = getchar();
    if (c == EOF) c = 0;
    *(uint8_t *)data = (uint8_t)c;

    // provera za k - modif
    // if (ctx->role == ROLE_WRITER && c == 'k') {
    //   for (int i = 0; i < ctx->hv->vm_count; i++) {
    //     if (ctx->hv->vms[i].role == ROLE_READER) {
    //       irq_queue_push(&ctx->hv->vms[i].irq_q, 33);
    //       pthread_kill(ctx->hv->threads[i], SIGUSR1);
    //     }
    //   }
    // }
    // atomic_store(&ctx->hv->shutdown_flag, 1);
  }

  return 0;
}

void serial_register_handlers(void) {
  io_register_handler(SERIAL_PORT, serial_handler);
}
