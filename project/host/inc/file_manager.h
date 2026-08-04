#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <stdint.h>
#include <stdio.h>

#define O_RD 1
#define O_WR 2
#define O_RDWR 4
#define O_CREATE 8

#define FSEEK_SET 1
#define FSEEK_END 2

#define MAX_FDS 16
#define MAX_FILENAME 128

struct file_descriptor {
  int in_use;
  char path[MAX_FILENAME];
  FILE *fp;
  int flags;
  int is_cow_copy;
};

struct file_manager {
  struct file_descriptor fds[MAX_FDS];
  int vm_id;
};

struct shared_file_registry;

void file_manager_init(struct file_manager *fm, int vm_id);
void file_manager_destroy(struct file_manager *fm);

int file_open(struct file_manager *fm, const char *path, int flags, struct shared_file_registry *sfr);
int file_close(struct file_manager *fm, int fd);
int file_read(struct file_manager *fm, int fd, char *buf, int count);
int file_write(struct file_manager *fm, int fd, const char *buf, int count, struct shared_file_registry *sfr);
int file_lseek(struct file_manager *fm, int fd, int offset, int whence);

void file_io_register_handlers(void);

#endif
