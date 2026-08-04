#ifndef SHARED_BUF_H
#define SHARED_BUF_H

#include <stdint.h>

#define SBUF_DATA_PORT 0x0510
#define SBUF_CTRL_PORT 0x0520

uint32_t sbuf_write_data(const char *data, uint32_t count);

#endif