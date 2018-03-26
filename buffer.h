#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <stdlib.h>

typedef struct buffer {
    
    char*  data;
    size_t alloc;
    size_t size;
    
} buffer_t;

void buf_grow(buffer_t* buf, size_t len);
void buf_free(buffer_t* buf);
void buf_append(buffer_t* buf, const void* src, size_t len);
void buf_read_file(buffer_t* buf, const char* filename, size_t max_length);

#endif
