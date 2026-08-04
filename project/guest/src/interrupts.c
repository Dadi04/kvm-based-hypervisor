#include "interrupts.h"
#include "descriptors.h"
#include "io.h"

static struct idt_entry idt[IDT_ENTRIES];

static int vm_role = -1;
static int role_received = 0;

int get_vm_role(void) { return vm_role; }

#define SBUF_DATA_PORT 0x0510
#define SBUF_CTRL_PORT 0x0520

static void serial_puts(const char *s) {
  for (; *s; ++s)
    outb(0xE9, (uint8_t)*s);
}

static void serial_put_int(long v) {
  char buf[24];
  int i = 0;
  int neg = v < 0;
  unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;

  do {
    buf[i++] = '0' + (u % 10);
    u /= 10;
  } while (u);

  if (neg)
    outb(0xE9, '-');
  while (i > 0)
    outb(0xE9, (uint8_t)buf[--i]);
}

static void __attribute__((interrupt, target("general-regs-only")))
irq0_handler(struct interrupt_frame *frame) {
  (void)frame;

  if (!role_received) {
    vm_role = (int)inb(SBUF_DATA_PORT);
    role_received = 1;

    serial_puts("IRQ32: role assigned = ");
    serial_put_int(vm_role);
    serial_puts(vm_role == 1 ? " (WRITER)\n" : " (READER)\n");
    return;
  }

  if (vm_role == 1) {
    serial_puts("IRQ32: writer signaled\n");
  } else {
    uint32_t count = 0;
    for (int i = 0; i < 4; i++) {
      uint8_t b = inb(SBUF_DATA_PORT);
      count |= ((uint32_t)b) << (8 * i);
    }

    serial_puts("IRQ32: reader got count=");
    serial_put_int((long)count);
    serial_puts("\n");

    static char read_buf[4096];
    uint32_t i;
    for (i = 0; i < count && i < sizeof(read_buf); i++) {
      read_buf[i] = (char)inb(SBUF_DATA_PORT);
    }

    outl(SBUF_CTRL_PORT, i);

    if (i < count) {
      serial_puts("IRQ32: ERROR – incomplete read, halting\n");
      asm volatile("hlt");
    }

    serial_puts("IRQ32: reader done, read ");
    serial_put_int((long)i);
    serial_puts(" bytes\n");

    asm volatile("hlt");
  }
}

// prekidna rutina za ulaz 33 - modif
// static void __attribute__((interrupt, target("general-regs-only")))
// irq33_handler(struct interrupt_frame *frame) {
//   (void)frame;
//   serial_puts("IRQ33: Interrupt request (entry 33), shutting VM...");
//   asm volatile("hlt");
// }

static void set_idt_gate(unsigned n, void (*handler)(struct interrupt_frame *)) {
  uint64_t addr = (uint64_t)(uintptr_t)handler;
  idt[n].offset_low = addr & 0xFFFF;
  idt[n].selector = 0x08;
  idt[n].ist = 0;
  idt[n].type_attr = 0x8E;
  idt[n].offset_mid = (addr >> 16) & 0xFFFF;
  idt[n].offset_high = (addr >> 32) & 0xFFFFFFFF;
  idt[n].reserved = 0;
}

void init_idt(void) {
  struct dt_ptr p;

  set_idt_gate(32, irq0_handler);
  // set_idt_gate(33, irq33_handler); nova kapija - modif

  p.limit = sizeof(idt) - 1;
  p.base = (uint64_t)(uintptr_t)idt;
  asm volatile("lidt %0" : : "m"(p) : "memory");
}
