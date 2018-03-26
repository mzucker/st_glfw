#ifndef _IMAGE_H_
#define _IMAGE_H_

#include "buffer.h"

int write_png(const char* filename,
             const unsigned char* data, 
             size_t ncols,
             size_t nrows,
             size_t rowsz,
             int yflip,
             const float* pixel_scale);

void read_jpg(const buffer_t* src,
              int vflip,
              size_t* width,
              size_t* height,
              size_t* size,
              buffer_t* dst);

void read_png(const buffer_t* src,
              int vflip,
              size_t* width,
              size_t* height,
              size_t* size,
              buffer_t* dst);


#endif
