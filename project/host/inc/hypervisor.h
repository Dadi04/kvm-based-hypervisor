#ifndef HYPERVISOR_H
#define HYPERVISOR_H

#include "cli.h"
#include "file_manager.h"
#include "irq.h"
#include "shared_buffer.h"
#include "shared_files.h"
#include "vm.h"

#include <pthread.h>
#include <stdatomic.h>

enum vm_role {
  ROLE_NONE = -1,
  ROLE_READER = 0,
  ROLE_WRITER = 1,
};

struct hypervisor;

struct vm_context {
  struct vm vm;
  int vm_id;
  const char *image_path;
  struct hypervisor *hv;
  enum vm_role role;
  struct file_manager fm;
  struct irq_queue irq_q;
  int role_assigned;
};

struct hypervisor {
  struct config *cfg;
  struct vm_context vms[MAX_GUESTS];
  pthread_t threads[MAX_GUESTS];
  int vm_count;
  atomic_int shutdown_flag;
  struct shared_file_registry sfr;
  struct shared_buffer sbuf;
  int writer_vm_id;
};

int hypervisor_init(struct hypervisor *hv, struct config *cfg);
int hypervisor_run(struct hypervisor *hv);
void hypervisor_shutdown(struct hypervisor *hv);
void hypervisor_destroy(struct hypervisor *hv);

#endif
