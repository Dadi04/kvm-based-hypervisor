#ifndef SHARED_FILES_H
#define SHARED_FILES_H

#include "cli.h"

struct shared_file_registry {
  int count;
  char *paths[MAX_SHARED_FILES];
};

void shared_files_init(struct shared_file_registry *sfr, struct config *cfg);

int is_shared_file(struct shared_file_registry *sfr, const char *path);

const char *cow_make_copy(struct shared_file_registry *sfr, const char *path, int vm_id);

#endif