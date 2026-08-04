#ifndef CLI_H
#define CLI_H

#include <stddef.h>

#define MAX_GUESTS 16
#define MAX_SHARED_FILES 16

struct config {
  size_t mem_size;
  size_t page_size;
  int guest_count;
  char *guest_images[MAX_GUESTS];
  int shared_file_count;
  char *shared_files[MAX_SHARED_FILES];
};

int parse_args(int argc, char **argv, struct config *cfg);

#endif
