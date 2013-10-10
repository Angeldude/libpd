/*
 *  Copyright (c) 2012 Peter Brinkmann (peter.brinkmann@gmail.com)
 *
 *  For information on usage and redistribution, and for a DISCLAIMER OF ALL
 *  WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 */

#include "ringbuffer.h"

#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <libkern/OSAtomic.h>
#endif

ring_buffer *rb_create(int size) {
  if (size & 0xff) return NULL;  // size must be a multiple of 256
  ring_buffer *buffer = malloc(sizeof(ring_buffer));
  if (!buffer) return NULL;
  buffer->buf_ptr = calloc(size, sizeof(char));
  if (!buffer->buf_ptr) {
    free(buffer);
    return NULL;
  }
  buffer->size = size;
  buffer->write_idx = 0;
  buffer->read_idx = 0;
  return buffer;
}

void rb_free(ring_buffer *buffer) {
  free(buffer->buf_ptr);
  free(buffer);
}

int rb_available_to_write(ring_buffer *buffer) {
  if (buffer) {
    // Note: The largest possible result is buffer->size - 1 because
    // we adopt the convention that read_idx == write_idx means that the
    // buffer is empty.
#ifndef __APPLE__
    int32_t read_idx = __sync_fetch_and_or(&(buffer->read_idx), 0);
    int32_t write_idx = __sync_fetch_and_or(&(buffer->write_idx), 0);
#else
    int32_t read_idx = OSAtomicOr32(0, &(buffer->read_idx));
    int32_t write_idx = OSAtomicOr32(0, &(buffer->write_idx));
#endif
    return (buffer->size + read_idx - write_idx - 1) % buffer->size;
  } else {
    return 0;
  }
}

int rb_available_to_read(ring_buffer *buffer) {
  if (buffer) {
#ifndef __APPLE__
    int32_t read_idx = __sync_fetch_and_or(&(buffer->read_idx), 0);
    int32_t write_idx = __sync_fetch_and_or(&(buffer->write_idx), 0);
#else
    int32_t read_idx = OSAtomicOr32Barrier(0, &(buffer->read_idx));
    int32_t write_idx = OSAtomicOr32(0, &(buffer->write_idx));
#endif
    return (buffer->size + write_idx - read_idx) % buffer->size;
  } else {
    return 0;
  }
}

int rb_write_to_buffer(ring_buffer *buffer, const char *src, int len) {
  if (len == 0) return 0;
  if (!buffer || len < 0 || len > rb_available_to_write(buffer)) return -1;
  int32_t write_idx = buffer->write_idx;  // No need for sync in writer thread.
  if (write_idx + len <= buffer->size) {
    memcpy(buffer->buf_ptr + write_idx, src, len);
  } else {
    int d = buffer->size - write_idx;
    memcpy(buffer->buf_ptr + write_idx, src, d);
    memcpy(buffer->buf_ptr, src + d, len - d);
  }
#ifndef __APPLE__
  __sync_val_compare_and_swap(&(buffer->write_idx), write_idx,
       (write_idx + len) % buffer->size);  // Includes memory barrier.
#else
  OSAtomicCompareAndSwap32Barrier(write_idx, (write_idx + len) % buffer->size,
      &(buffer->write_idx));
#endif
  return 0; 
}

int rb_read_from_buffer(ring_buffer *buffer, char *dest, int len) {
  if (len == 0) return 0;
  if (!buffer || len < 0 || len > rb_available_to_read(buffer)) return -1;
  // Note that rb_available_to_read also serves as a memory barrier, and so any
  // writes to buffer->buf_ptr that precede the update of buffer->write_idx are
  // visible to us now.
  int32_t read_idx = buffer->read_idx;  // No need for sync in reader thread.
  if (read_idx + len <= buffer->size) {
    memcpy(dest, buffer->buf_ptr + read_idx, len);
  } else {
    int d = buffer->size - read_idx;
    memcpy(dest, buffer->buf_ptr + read_idx, d);
    memcpy(dest + d, buffer->buf_ptr, len - d);
  }
#ifndef __APPLE__
  __sync_val_compare_and_swap(&(buffer->read_idx), read_idx,
       (read_idx + len) % buffer->size);
#else
  OSAtomicCompareAndSwap32(read_idx, (read_idx + len) % buffer->size,
      &(buffer->read_idx));
#endif
  return 0; 
}
