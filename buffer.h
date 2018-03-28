#ifndef _BUFFER_H_
#define _BUFFER_H_

#include <stdlib.h>

typedef struct buffer {
    
    char*  data;
    size_t alloc;
    size_t size;
    
} buffer_t;

enum {
    BUF_RAW_APPEND = 0,
    BUF_NULL_TERMINATE = 1
};

void buf_grow(buffer_t* buf, size_t len);

void buf_free(buffer_t* buf);

void buf_append_mem(buffer_t* buf, const void* src,
                    size_t len, int append_type);

void buf_append_file(buffer_t* buf, const char* filename,
                     size_t max_length, int append_type);

#endif
