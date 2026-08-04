#include "shared_files.h"

#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void shared_files_init(struct shared_file_registry *sfr, struct config *cfg) {
  sfr->count = cfg->shared_file_count;
  for (int i = 0; i < sfr->count; i++) {
    sfr->paths[i] = cfg->shared_files[i];
  }
}

int is_shared_file(struct shared_file_registry *sfr, const char *path) {
  for (int i = 0; i < sfr->count; i++) {
    char tmp[256];
    strncpy(tmp, sfr->paths[i], sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    const char *base = basename(tmp);
    if (strcmp(base, path) == 0)
      return i;
  }
  return -1;
}

const char *cow_make_copy(struct shared_file_registry *sfr, const char *path, int vm_id) {
  char *cow_path = malloc(512);
  if (!cow_path) {
    return NULL;
  }

  int idx = is_shared_file(sfr, path);
  if (idx < 0) {
    return NULL;
  }

  const char *src_path = sfr->paths[idx];

  snprintf(cow_path, 512, "%s_vm%d_cow", src_path, vm_id);

  FILE *src = fopen(src_path, "rb");
  if (!src) {
    perror("COW: failed to open source");
    return NULL;
  }

  FILE *dst = fopen(cow_path, "wb");
  if (!dst) {
    perror("COW: failed to create copy");
    fclose(src);
    return NULL;
  }

  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
    if (fwrite(buf, 1, n, dst) != n) {
      perror("COW: write error");
      fclose(src);
      fclose(dst);
      return NULL;
    }
  }

  fclose(src);
  fclose(dst);

  return cow_path;
}
