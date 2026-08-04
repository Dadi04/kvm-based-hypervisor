#include "file_api.h"
#include "io.h"

#define FILE_PORT 0x0278

int open(const char *path, int flags) {
  outb(FILE_PORT, 1);

  outb(FILE_PORT, (uint8_t)flags);

  const char *p;
  for (p = path; *p; ++p)
    outb(FILE_PORT, (uint8_t)*p);
  outb(FILE_PORT, 0);

  return (int)(int8_t)inb(FILE_PORT);
}

int close(int fd) {
  outb(FILE_PORT, 2);

  outb(FILE_PORT, (uint8_t)fd);

  return (int)(int8_t)inb(FILE_PORT);
}

int read(int fd, char *buf, int count) {
  outb(FILE_PORT, 3);

  outb(FILE_PORT, (uint8_t)fd);

  outb(FILE_PORT, (uint8_t)count);

  int n = (int)(int8_t)inb(FILE_PORT);
  if (n <= 0)
    return n;

  for (int i = 0; i < n; i++)
    buf[i] = (char)inb(FILE_PORT);

  return n;
}

int write(int fd, const char *buf, int count) {
  outb(FILE_PORT, 4);

  outb(FILE_PORT, (uint8_t)fd);

  outb(FILE_PORT, (uint8_t)count);

  for (int i = 0; i < count; i++)
    outb(FILE_PORT, (uint8_t)buf[i]);

  return (int)(int8_t)inb(FILE_PORT);
}

int lseek(int fd, const int offset, int off_flag) {
  outb(FILE_PORT, 5);

  outb(FILE_PORT, (uint8_t)fd);

  outb(FILE_PORT, (uint8_t)offset);

  outb(FILE_PORT, (uint8_t)off_flag);

  return (int)(int8_t)inb(FILE_PORT);
}
