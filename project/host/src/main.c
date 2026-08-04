#include "cli.h"
#include "hypervisor.h"

#include <signal.h>
#include <stdio.h>

// omogucava budjenje niti blokirane u KVM_RUN
static void sigusr1_handler(int signum) { (void)signum; }

int main(int argc, char *argv[]) {
  struct config cfg; // podesavanja sa komandne linije
  struct hypervisor hv;

  struct sigaction sa = {0};
  sa.sa_handler = sigusr1_handler;
  sigaction(SIGUSR1, &sa, NULL);

  if (parse_args(argc, argv, &cfg) < 0) {
    return 1;
  }

  if (hypervisor_init(&hv, &cfg) < 0) {
    fprintf(stderr, "Failed to initialize hypervisor\n");
    return 1;
  }

  int rc = hypervisor_run(&hv);

  hypervisor_destroy(&hv);

  return rc == 0 ? 0 : 1;
}
