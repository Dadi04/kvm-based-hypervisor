#ifndef SHARED_BUFFER_H
#define SHARED_BUFFER_H

#include <pthread.h>
#include <stdint.h>

#define BUFFER_SIZE 4096

struct shared_buffer {
  char data[BUFFER_SIZE];
  uint32_t data_size;
  int total_readers;
  int readers_done;
  int data_ready;
  pthread_mutex_t lock;
  pthread_cond_t cond_data_ready;
  pthread_cond_t cond_all_read;
};

void sbuf_init(struct shared_buffer *sb, int total_readers);
void sbuf_destroy(struct shared_buffer *sb);

uint32_t sbuf_write(struct shared_buffer *sb, const char *data, uint32_t count);

void sbuf_notify_readers(struct shared_buffer *sb);

void sbuf_wait_all_readers(struct shared_buffer *sb);

void sbuf_reader_wait(struct shared_buffer *sb, char *out_data, uint32_t *out_size);

void sbuf_reader_done(struct shared_buffer *sb);

#endif
