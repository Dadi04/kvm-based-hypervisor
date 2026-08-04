#ifndef IO_HANDLER_H
#define IO_HANDLER_H

#include <linux/kvm.h>
#include <stdint.h>

struct vm_context;

typedef int (*io_handler_fn)(struct vm_context *ctx, int direction, uint16_t port, void *data, uint32_t size);

int io_register_handler(uint16_t port, io_handler_fn handler);

int io_dispatch(struct vm_context *ctx, struct kvm_run *run);

#endif