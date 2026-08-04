#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

struct vm;
struct vm_context;

#define IRQ_QUEUE_SIZE 16

struct irq_queue {
  unsigned int vectors[IRQ_QUEUE_SIZE];
  int head;
  int tail;
  int count;
};

void irq_queue_init(struct irq_queue *q);

int irq_queue_push(struct irq_queue *q, unsigned int vector);

int irq_queue_pending(struct irq_queue *q);

int irq_handle_window(struct vm_context *ctx);

void irq_request_window(struct vm *v);

#endif
