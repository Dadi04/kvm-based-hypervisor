#include "irq.h"
#include "hypervisor.h"
#include "vm.h"

#include <linux/kvm.h>
#include <stdio.h>
#include <sys/ioctl.h>

void irq_queue_init(struct irq_queue *q) {
  q->head = 0;
  q->tail = 0;
  q->count = 0;
}

int irq_queue_push(struct irq_queue *q, unsigned int vector) {
  if (q->count >= IRQ_QUEUE_SIZE) {
    fprintf(stderr, "IRQ queue full, dropping vector %u\n", vector);
    return -1;
  }

  q->vectors[q->tail] = vector;
  q->tail = (q->tail + 1) % IRQ_QUEUE_SIZE;
  q->count++;
  return 0;
}

static unsigned int irq_queue_pop(struct irq_queue *q) {
  if (q->count == 0)
    return 0;

  unsigned int v = q->vectors[q->head];
  q->head = (q->head + 1) % IRQ_QUEUE_SIZE;
  q->count--;
  return v;
}

int irq_queue_pending(struct irq_queue *q) { return q->count > 0; }

void irq_request_window(struct vm *v) { v->run->request_interrupt_window = 1; }

int irq_handle_window(struct vm_context *ctx) {
  struct vm *v = &ctx->vm;

  if (!irq_queue_pending(&ctx->irq_q)) {
    v->run->request_interrupt_window = 0;
    return 0;
  }

  unsigned int vector = irq_queue_pop(&ctx->irq_q);

  if (inject_irq(v, vector) < 0) {
    fprintf(stderr, "[VM %d] Failed to inject IRQ vector %u\n", ctx->vm_id, vector);
    return -1;
  }

  v->run->request_interrupt_window = irq_queue_pending(&ctx->irq_q);

  return 0;
}
