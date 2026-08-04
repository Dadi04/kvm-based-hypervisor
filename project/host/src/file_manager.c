#include "file_manager.h"
#include "hypervisor.h"
#include "io_handler.h"
#include "shared_files.h"

#include <ctype.h>
#include <linux/kvm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void file_manager_init(struct file_manager *fm, int vm_id) {
  memset(fm, 0, sizeof(*fm));
  fm->vm_id = vm_id;
}

void file_manager_destroy(struct file_manager *fm) {
  for (int i = 0; i < MAX_FDS; i++) {
    if (fm->fds[i].in_use && fm->fds[i].fp) {
      fclose(fm->fds[i].fp);
      fm->fds[i].fp = NULL;
      fm->fds[i].in_use = 0;
    }
  }
}

static int validate_filename(const char *name) {
  if (!name || name[0] == '\0') {
    return 0;
  }

  if (!isalpha((unsigned char)name[0])) {
    return 0;
  }

  for (int i = 0; name[i]; i++) {
    char c = name[i];
    if (!isalpha((unsigned char)c) && !isdigit((unsigned char)c) && c != '.') {
      return 0;
    }
  }
  return 1;
}

static int alloc_fd(struct file_manager *fm) {
  for (int i = 0; i < MAX_FDS; i++) {
    if (!fm->fds[i].in_use) {
      return i;
    }
  }
  return -1;
}

static struct file_descriptor *get_fd(struct file_manager *fm, int fd) {
  if (fd < 0 || fd >= MAX_FDS || !fm->fds[fd].in_use)
    return NULL;
  return &fm->fds[fd];
}

static void build_local_path(char *out, size_t out_sz, int vm_id, const char *name) {
  snprintf(out, out_sz, "vm%d_%s", vm_id, name);
}

int file_open(struct file_manager *fm, const char *path, int flags, struct shared_file_registry *sfr) {
  if (!validate_filename(path)) {
    return -1;
  }

  int fd_idx = alloc_fd(fm);
  if (fd_idx < 0) {
    return -1;
  }

  struct file_descriptor *fde = &fm->fds[fd_idx];
  int shared_idx = is_shared_file(sfr, path);
  int is_shared = (shared_idx >= 0);

  char host_path[512];

  if (is_shared) {
    strncpy(host_path, sfr->paths[shared_idx], sizeof(host_path) - 1);
    host_path[sizeof(host_path) - 1] = '\0';
  } else {
    build_local_path(host_path, sizeof(host_path), fm->vm_id, path);
  }

  const char *mode;
  if (is_shared) {
    mode = "rb";
  } else if (flags & O_CREATE) {
    if ((flags & O_RDWR) || ((flags & O_RD) && (flags & O_WR)))
      mode = "w+b";
    else if (flags & O_WR)
      mode = "wb";
    else
      mode = "rb";
  } else {
    if ((flags & O_RDWR) || ((flags & O_RD) && (flags & O_WR)))
      mode = "r+b";
    else if (flags & O_WR)
      mode = "r+b";
    else
      mode = "rb";
  }

  FILE *fp = fopen(host_path, mode);
  if (!fp) {
    return -1;
  }

  fseek(fp, 0, SEEK_SET);

  fde->in_use = 1;
  fde->fp = fp;
  fde->flags = flags;
  fde->is_cow_copy = 0;
  strncpy(fde->path, path, MAX_FILENAME - 1);
  fde->path[MAX_FILENAME - 1] = '\0';

  return fd_idx;
}

int file_close(struct file_manager *fm, int fd) {
  struct file_descriptor *fde = get_fd(fm, fd);
  if (!fde) {
    return -1;
  }

  if (fde->fp) {
    fclose(fde->fp);
    fde->fp = NULL;
  }
  fde->in_use = 0;
  return 0;
}

int file_read(struct file_manager *fm, int fd, char *buf, int count) {
  struct file_descriptor *fde = get_fd(fm, fd);
  if (!fde) {
    return -1;
  }

  if (!(fde->flags & O_RD) && !(fde->flags & O_RDWR)) {
    return -1;
  }

  size_t n = fread(buf, 1, (size_t)count, fde->fp);
  return (int)n;
}

int file_write(struct file_manager *fm, int fd, const char *buf, int count, struct shared_file_registry *sfr) {
  struct file_descriptor *fde = get_fd(fm, fd);
  if (!fde) {
    return -1;
  }

  if (!(fde->flags & O_WR) && !(fde->flags & O_RDWR)) {
    return -1;
  }

  if (!fde->is_cow_copy && is_shared_file(sfr, fde->path) >= 0) {
    long pos = ftell(fde->fp);

    const char *cow_path = cow_make_copy(sfr, fde->path, fm->vm_id);
    if (!cow_path) {
      return -1;
    }

    fclose(fde->fp);

    fde->fp = fopen(cow_path, "r+b");
    if (!fde->fp) {
      free((void *)cow_path);
      return -1;
    }

    fseek(fde->fp, pos, SEEK_SET);
    fde->is_cow_copy = 1;
    free((void *)cow_path);
  }

  size_t n = fwrite(buf, 1, (size_t)count, fde->fp);
  fflush(fde->fp);
  return (int)n;
}

int file_lseek(struct file_manager *fm, int fd, int offset, int where) {
  struct file_descriptor *fde = get_fd(fm, fd);
  if (!fde) {
    return -1;
  }

  int origin;
  switch (where) {
    case FSEEK_SET:
      origin = SEEK_SET;
      break;
    case FSEEK_END:
      origin = SEEK_END;
      offset = 0;
      break;
    default:
      return -1;
  }

  if (fseek(fde->fp, offset, origin) < 0) {
    return -1;
  }

  return (int)ftell(fde->fp);
}

#define FILE_PORT 0x0278

enum file_opcode {
  FOPCODE_NONE = 0,
  FOPCODE_OPEN = 1,
  FOPCODE_CLOSE = 2,
  FOPCODE_READ = 3,
  FOPCODE_WRITE = 4,
  FOPCODE_LSEEK = 5,
};

enum file_phase {
  FPHASE_OPCODE,
  FPHASE_OPEN_FLAGS,
  FPHASE_OPEN_PATH,
  FPHASE_OPEN_RESULT,
  FPHASE_CLOSE_FD,
  FPHASE_CLOSE_RESULT,
  FPHASE_READ_FD,
  FPHASE_READ_COUNT,
  FPHASE_READ_RESULT,
  FPHASE_READ_DATA,
  FPHASE_WRITE_FD,
  FPHASE_WRITE_COUNT,
  FPHASE_WRITE_DATA,
  FPHASE_WRITE_RESULT,
  FPHASE_LSEEK_FD,
  FPHASE_LSEEK_OFFSET,
  FPHASE_LSEEK_WHENCE,
  FPHASE_LSEEK_RESULT,
};

struct file_proto_state {
  enum file_opcode opcode;
  enum file_phase phase;
  int fd;
  int flags;
  char path_buf[MAX_FILENAME];
  int path_idx;
  char data_buf[4096];
  int data_count;
  int data_idx;
  int result;
};

static struct file_proto_state proto_states[MAX_GUESTS];

static int file_io_handler(struct vm_context *ctx, int direction, uint16_t port, void *data, uint32_t size) {
  (void)port;
  (void)size;

  struct file_proto_state *ps = &proto_states[ctx->vm_id];
  uint8_t byte;

  if (direction == KVM_EXIT_IO_OUT) { // guest -> stdout
    byte = *(uint8_t *)data;

    switch (ps->phase) {
      case FPHASE_OPCODE:
        ps->opcode = (enum file_opcode)byte;
        ps->path_idx = 0;
        ps->data_idx = 0;
        switch (ps->opcode) {
          case FOPCODE_OPEN:
            ps->phase = FPHASE_OPEN_FLAGS;
            break;
          case FOPCODE_CLOSE:
            ps->phase = FPHASE_CLOSE_FD;
            break;
          case FOPCODE_READ:
            ps->phase = FPHASE_READ_FD;
            break;
          case FOPCODE_WRITE:
            ps->phase = FPHASE_WRITE_FD;
            break;
          case FOPCODE_LSEEK:
            ps->phase = FPHASE_LSEEK_FD;
            break;
          default:
            ps->phase = FPHASE_OPCODE;
            break;
        }
        break;

      case FPHASE_OPEN_FLAGS:
        ps->flags = byte;
        ps->phase = FPHASE_OPEN_PATH;
        break;

      case FPHASE_OPEN_PATH:
        if (byte == 0 || ps->path_idx >= MAX_FILENAME - 1) {
          ps->path_buf[ps->path_idx] = '\0';
          ps->result = file_open(&ctx->fm, ps->path_buf, ps->flags, &ctx->hv->sfr);
          ps->phase = FPHASE_OPEN_RESULT;
        } else {
          ps->path_buf[ps->path_idx++] = (char)byte;
        }
        break;

      case FPHASE_CLOSE_FD:
        ps->fd = byte;
        ps->result = file_close(&ctx->fm, ps->fd);
        ps->phase = FPHASE_CLOSE_RESULT;
        break;

      case FPHASE_READ_FD:
        ps->fd = byte;
        ps->phase = FPHASE_READ_COUNT;
        break;

      case FPHASE_READ_COUNT:
        ps->data_count = byte;
        if (ps->data_count > (int)sizeof(ps->data_buf))
          ps->data_count = (int)sizeof(ps->data_buf);
        ps->result = file_read(&ctx->fm, ps->fd, ps->data_buf, ps->data_count);
        ps->data_idx = 0;
        ps->phase = FPHASE_READ_RESULT;
        break;

      case FPHASE_WRITE_FD:
        ps->fd = byte;
        ps->phase = FPHASE_WRITE_COUNT;
        break;

      case FPHASE_WRITE_COUNT:
        ps->data_count = byte;
        if (ps->data_count > (int)sizeof(ps->data_buf))
          ps->data_count = (int)sizeof(ps->data_buf);
        ps->data_idx = 0;
        ps->phase = FPHASE_WRITE_DATA;
        break;

      case FPHASE_WRITE_DATA:
        ps->data_buf[ps->data_idx++] = (char)byte;
        if (ps->data_idx >= ps->data_count) {
          ps->result = file_write(&ctx->fm, ps->fd, ps->data_buf, ps->data_count, &ctx->hv->sfr);
          ps->phase = FPHASE_WRITE_RESULT;
        }
        break;

      case FPHASE_LSEEK_FD:
        ps->fd = byte;
        ps->phase = FPHASE_LSEEK_OFFSET;
        break;

      case FPHASE_LSEEK_OFFSET:
        ps->data_count = byte;
        ps->phase = FPHASE_LSEEK_WHENCE;
        break;

      case FPHASE_LSEEK_WHENCE:
        ps->result = file_lseek(&ctx->fm, ps->fd, ps->data_count, byte);
        ps->phase = FPHASE_LSEEK_RESULT;
        break;

      default:
        ps->phase = FPHASE_OPCODE;
        break;
      }

  } else { // stdin -> guest
    switch (ps->phase) {
      case FPHASE_OPEN_RESULT:
      case FPHASE_CLOSE_RESULT:
      case FPHASE_WRITE_RESULT:
      case FPHASE_LSEEK_RESULT:
        *(int8_t *)data = (int8_t)ps->result;
        ps->phase = FPHASE_OPCODE;
        break;

      case FPHASE_READ_RESULT:
        *(int8_t *)data = (int8_t)ps->result;
        if (ps->result > 0) {
          ps->phase = FPHASE_READ_DATA;
        } else {
          ps->phase = FPHASE_OPCODE;
        }
        break;

      case FPHASE_READ_DATA:
        *(uint8_t *)data = (uint8_t)ps->data_buf[ps->data_idx++];
        if (ps->data_idx >= ps->result)
          ps->phase = FPHASE_OPCODE;
        break;

      default:
        *(uint8_t *)data = 0xFF;
        ps->phase = FPHASE_OPCODE;
        break;
      }
  }

  return 0;
}

void file_io_register_handlers(void) {
  memset(proto_states, 0, sizeof(proto_states));
  io_register_handler(FILE_PORT, file_io_handler);
}
