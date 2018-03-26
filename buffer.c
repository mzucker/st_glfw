#include "buffer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

//////////////////////////////////////////////////////////////////////

void buf_grow(buffer_t* buf, size_t len) {

    size_t new_size = buf->size + len;
    
    if (!buf->data) {
        
        buf->data = malloc(len);
        buf->alloc = len;

    } else if (buf->alloc < new_size) {

        while (buf->alloc < new_size) {
            buf->alloc *= 2;
        }
        buf->data = realloc(buf->data, buf->alloc);

    }

    assert(buf->alloc >= new_size);

}

//////////////////////////////////////////////////////////////////////

void buf_free(buffer_t* buf) {

    if (buf->data) { free(buf->data); }
    memset(buf, 0, sizeof(buffer_t));

}

//////////////////////////////////////////////////////////////////////

void buf_append(buffer_t* buf, const void* src, size_t len) {

    buf_grow(buf, len+1);
    
    memcpy(buf->data + buf->size, src, len);
    
    buf->size += len;
    buf->data[buf->size] = 0;

}

//////////////////////////////////////////////////////////////////////

void buf_read_file(buffer_t* buf, const char* filename,
                   size_t max_length) {

    FILE* fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "error opening %s\n\n", filename);
        exit(1);
    }
    
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);

    if (fsize < 0 || fsize > max_length) {
        fprintf(stderr, "file exceeds maximum size!\n\n");
        exit(1);
    }
  
    fseek(fp, 0, SEEK_SET);
    
    buf_grow(buf, fsize+1);

    int nread = fread(buf->data + buf->size, fsize, 1, fp);

    if (nread != 1) {
        fprintf(stderr, "error reading %s\n\n", filename);
        exit(1);
    }

    buf->size += fsize;
    buf->data[buf->size] = 0;

}
