#include "shared_buf.h"
#include "io.h"

uint32_t sbuf_write_data(const char *data, uint32_t count) {
  for (int i = 0; i < 4; i++) {
    outb(SBUF_DATA_PORT, (uint8_t)(count >> (8 * i)));
  }

  for (uint32_t i = 0; i < count; i++) {
    outb(SBUF_DATA_PORT, (uint8_t)data[i]);
  }

  uint32_t written = inl(SBUF_CTRL_PORT);
  return written;
}
