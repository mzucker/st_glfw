#include "image.h"
#include "require.h"

#include <stdio.h>
#include <png.h>
#include <jpeglib.h>
#include <string.h>

int write_png(const char* filename,
              const unsigned char* data, 
              size_t ncols,
              size_t nrows,
              size_t rowsz,
              int yflip,
              const float* pixel_scale) {

    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "error opening %s for output\n", filename);
        return 0;
    }
  
    png_structp png_ptr = png_create_write_struct
        (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr) {
        fprintf(stderr, "error creating write struct\n");
        fclose(fp);
        return 0;
    }
  
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fprintf(stderr, "error creating info struct\n");
        png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
        fclose(fp);
        return 0;
    }  

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "error processing PNG\n");
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return 0;
    }

    png_init_io(png_ptr, fp);

    png_set_IHDR(png_ptr, info_ptr, 
                 ncols, nrows,
                 8, 
                 PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);

    const float base_res = 72./.0254;

    if (pixel_scale) {
        
        int res_x = base_res * pixel_scale[0];
        int res_y = base_res * pixel_scale[1];

        png_set_pHYs(png_ptr, info_ptr,
                     res_x, res_y,
                     PNG_RESOLUTION_METER);

    }

    png_write_info(png_ptr, info_ptr);

    const unsigned char* rowptr = data + (yflip ? rowsz*(nrows-1) : 0);
    int rowdelta = rowsz * (yflip ? -1 : 1);

    for (size_t y=0; y<nrows; ++y) {
        png_write_row(png_ptr, (png_bytep)rowptr);
        rowptr += rowdelta;
    }

    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);

    fprintf(stderr, "wrote %s\n", filename);

    return 1;

}

//////////////////////////////////////////////////////////////////////

unsigned char* get_rowptr_and_delta(buffer_t* dst,
                                    int height, int stride,
                                    int vflip,
                                    int* row_delta) {

    unsigned char* dstart = (unsigned char*)dst->data + dst->size;

    if (vflip) {
        *row_delta = -stride;
        return dstart + (height-1)*stride;
    } else {
        *row_delta = stride;
        return dstart;
    }

}


//////////////////////////////////////////////////////////////////////

void read_jpg(const buffer_t* raw,
              int vflip,
              size_t* pchannels,
              size_t* pwidth,
              size_t* pheight,
              size_t* psize,
              buffer_t* dst) {

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr mgr;
    jmp_buf setjmp_buffer;

    cinfo.err = jpeg_std_error(&mgr);

    if (setjmp(setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
    }

    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, (unsigned char*)raw->data, raw->size);

    int rc = jpeg_read_header(&cinfo, TRUE);

    if (rc != 1) {
        fprintf(stderr, "failure reading jpeg header!\n");
        exit(1);
    }

    jpeg_start_decompress(&cinfo);

    int width = cinfo.output_width;
    int height = cinfo.output_height;
    int pixel_size = cinfo.output_components;

    printf("jpeg is %dx%dx%d\n", width, height, pixel_size);

    if (width <= 0 || height <= 0 || pixel_size != 3) {
        fprintf(stderr, "incorrect JPG type!\n");
        exit(1);
    }

    int size = width * height * pixel_size;
    int row_stride = width * pixel_size;

    if (row_stride % 4) {
        fprintf(stderr, "warning: bad stride for GL_UNPACK_ALIGNMENT!\n");
    }
        
    buf_grow(dst, size);

    int row_delta;
    unsigned char* rowptr = get_rowptr_and_delta(dst, height, row_stride,
                                                 vflip, &row_delta);

    dst->size += size;

    while (cinfo.output_scanline < cinfo.output_height) {
        
        jpeg_read_scanlines(&cinfo, &rowptr, 1);
        rowptr += row_delta;
        
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    *pchannels = pixel_size;
    *pwidth = width;
    *pheight = height;
    *psize = size;

}

//////////////////////////////////////////////////////////////////////

typedef struct png_simple_stream {

    const unsigned char* start;
    size_t pos;
    size_t len;
    
} png_simple_stream_t;

//////////////////////////////////////////////////////////////////////

void png_stream_read(png_structp png_ptr,
                     png_bytep data,
                     png_size_t length) {


    png_simple_stream_t* str = (png_simple_stream_t*)png_get_io_ptr(png_ptr);

    require( str->pos + length <= str->len );

    memcpy(data, str->start + str->pos, length );
    str->pos += length;

}

//////////////////////////////////////////////////////////////////////

void read_png(const buffer_t* raw,
              int vflip,
              size_t* pchannels,
              size_t* pwidth,
              size_t* pheight,
              size_t* psize,
              buffer_t* dst) {
    
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                                 NULL, NULL, NULL);

    if (!png_ptr) {
        fprintf(stderr, "error initializing png read struct!\n");
        exit(1);
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);

    if (!info_ptr) {
        fprintf(stderr, "error initializing png info struct!\n");
        exit(1);
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "PNG read error!\n");
        exit(1);
    }

    png_simple_stream_t str = { (const unsigned char*)raw->data, 0, raw->size };

    png_set_read_fn(png_ptr, &str, png_stream_read);

    png_set_sig_bytes(png_ptr, 0);
    png_read_info(png_ptr, info_ptr);

    int width = png_get_image_width(png_ptr, info_ptr);
    int height = png_get_image_height(png_ptr, info_ptr);
    int bitdepth = png_get_bit_depth(png_ptr, info_ptr);
    int channels = png_get_channels(png_ptr, info_ptr);
    int color_type = png_get_color_type(png_ptr, info_ptr);

    const char* color_type_str = "[unknown]";

#define HANDLE(x) case x: color_type_str = #x; break
    switch (color_type) {
        HANDLE(PNG_COLOR_TYPE_GRAY);
        HANDLE(PNG_COLOR_TYPE_GRAY_ALPHA);
        HANDLE(PNG_COLOR_TYPE_RGB);
        HANDLE(PNG_COLOR_TYPE_RGB_ALPHA);
    }

    printf("PNG is %dx%dx%d, color type %s\n",
           width, height, channels, color_type_str);
    
    if (width <= 0 || height <= 0 || bitdepth != 8 || (channels != 3 && channels != 4)) {
        fprintf(stderr, "invalid PNG settings!\n");
        exit(1);
    }

    int row_stride = width * channels;
    
    if (row_stride % 4) {
        fprintf(stderr, "warning: PNG data is not aligned for OpenGL!\n");
        exit(1);
    }

    int size = row_stride * height;

    *pwidth = width;
    *pheight = height;
    *psize = size;
    *pchannels = channels;

    buf_grow(dst, size);

    int row_delta;
    unsigned char* rowptr = get_rowptr_and_delta(dst, height, row_stride,
                                                 vflip, &row_delta);

    dst->size += size;

    png_bytepp row_ptrs = malloc(height * sizeof(png_bytep));
    
    for (size_t i=0; i<height; ++i) {
        row_ptrs[i] = rowptr;
        rowptr += row_delta;
    }

    png_read_image(png_ptr, row_ptrs);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    free(row_ptrs);

}
