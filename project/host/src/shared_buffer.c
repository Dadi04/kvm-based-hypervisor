#include "shared_buffer.h"

#include <stdio.h>
#include <string.h>

void sbuf_init(struct shared_buffer *sb, int total_readers) {
  memset(sb->data, 0, BUFFER_SIZE);
  sb->data_size = 0;
  sb->total_readers = total_readers;
  sb->readers_done = 0;
  sb->data_ready = 0;

  pthread_mutex_init(&sb->lock, NULL);
  pthread_cond_init(&sb->cond_data_ready, NULL);
  pthread_cond_init(&sb->cond_all_read, NULL);
}

void sbuf_destroy(struct shared_buffer *sb) {
  pthread_cond_destroy(&sb->cond_all_read);
  pthread_cond_destroy(&sb->cond_data_ready);
  pthread_mutex_destroy(&sb->lock);
}

uint32_t sbuf_write(struct shared_buffer *sb, const char *data, uint32_t count) {
  pthread_mutex_lock(&sb->lock);

  if (count > BUFFER_SIZE)
    count = BUFFER_SIZE;

  memcpy(sb->data, data, count);
  sb->data_size = count;

  pthread_mutex_unlock(&sb->lock);
  return count;
}

void sbuf_notify_readers(struct shared_buffer *sb) {
  pthread_mutex_lock(&sb->lock);
  sb->data_ready = 1;
  sb->readers_done = 0;
  pthread_cond_broadcast(&sb->cond_data_ready);
  pthread_mutex_unlock(&sb->lock);
}

void sbuf_wait_all_readers(struct shared_buffer *sb) {
  pthread_mutex_lock(&sb->lock);
  while (sb->readers_done < sb->total_readers) {
    pthread_cond_wait(&sb->cond_all_read, &sb->lock);
  }

  sb->data_ready = 0;
  pthread_mutex_unlock(&sb->lock);
}

void sbuf_reader_wait(struct shared_buffer *sb, char *out_data, uint32_t *out_size) {
  pthread_mutex_lock(&sb->lock);
  while (!sb->data_ready) {
    pthread_cond_wait(&sb->cond_data_ready, &sb->lock);
  }

  memcpy(out_data, sb->data, sb->data_size);
  *out_size = sb->data_size;

  pthread_mutex_unlock(&sb->lock);
}

void sbuf_reader_done(struct shared_buffer *sb) {
  pthread_mutex_lock(&sb->lock);
  sb->readers_done++;
  if (sb->readers_done >= sb->total_readers) {
    pthread_cond_signal(&sb->cond_all_read);
  }
  pthread_mutex_unlock(&sb->lock);
}
