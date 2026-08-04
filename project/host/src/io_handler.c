#include "io_handler.h"

#include <stdio.h>
#include <string.h>

#define MAX_IO_HANDLERS 32

struct io_entry {
  uint16_t port;
  io_handler_fn handler;
  int used;
};

static struct io_entry io_table[MAX_IO_HANDLERS];
static int io_table_count = 0;

int io_register_handler(uint16_t port, io_handler_fn handler) {
  for (int i = 0; i < io_table_count; i++) {
    if (io_table[i].used && io_table[i].port == port) {
      io_table[i].handler = handler;
      return 0;
    }
  }

  if (io_table_count >= MAX_IO_HANDLERS) {
    fprintf(stderr, "IO handler table full (max %d)\n", MAX_IO_HANDLERS);
    return -1;
  }

  io_table[io_table_count].port = port;
  io_table[io_table_count].handler = handler;
  io_table[io_table_count].used = 1;
  io_table_count++;
  return 0;
}

int io_dispatch(struct vm_context *ctx, struct kvm_run *run) {
  uint16_t port = run->io.port;
  int direction = run->io.direction;
  uint32_t size = run->io.size;
  void *data = (char *)run + run->io.data_offset;

  for (int i = 0; i < io_table_count; i++) {
    if (io_table[i].used && io_table[i].port == port) {
      return io_table[i].handler(ctx, direction, port, data, size);
    }
  }

  fprintf(stderr, "Warning: unhandled IO port 0x%04X (dir=%s, size=%u)\n", port, direction == KVM_EXIT_IO_IN ? "IN" : "OUT", size);
  return 1;
}
