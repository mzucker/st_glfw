#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <ctype.h>

#include <assert.h>

#include "buffer.h"
#include "image.h"
#include "www.h"
#include "stringutils.h"

enum {
    
    MAX_UNIFORMS = 16,
    
    NUM_CHANNELS = 4,
    MAX_CHANNEL_NAME_LENGTH = 16,
    MAX_CHANNEL_DECL_LENGTH = 64,
    
    BIG_STRING_LENGTH = 1024,
    MAX_PROGRAM_LENGTH = 1024*256,
    MAX_FILE_LENGTH = 1024*1024*1024, // 1GB
    
    KEYMAP_ROWS = 3,
    KEYMAP_BYTES_PER_ROW = 256*3,
    KEYMAP_TOTAL_BYTES = KEYMAP_ROWS * KEYMAP_BYTES_PER_ROW
    
};

//////////////////////////////////////////////////////////////////////

typedef void (*glUniformFloatFunc)(GLint, GLsizei, const GLfloat*);
typedef void (*glUniformIntFunc)(GLint, GLsizei, const GLint*);

typedef struct uniform {
    
    const char* name;
    GLuint handle;
    const void* src;
    GLenum ptr_type;

    union {
        glUniformFloatFunc float_func;
        glUniformIntFunc   int_func;
    };
    
} uniform_t;

uniform_t uinfo[MAX_UNIFORMS];
int num_uniforms = 0;

//////////////////////////////////////////////////////////////////////

typedef enum texture_ctype {
    CTYPE_NONE = 0,
    CTYPE_TEXTURE = 1,
    CTYPE_KEYBOARD = 2,
    CTYPE_CUBEMAP = 3,
} texture_ctype_t;

typedef struct channel {

    texture_ctype_t ctype;
    char name[MAX_CHANNEL_NAME_LENGTH];

    GLuint target;
    GLuint tex_id;
    
    int filter;
    int srgb;
    int vflip;
    int wrap;

    size_t channels, width, height, size;
    buffer_t texture;

    int dirty;
    int initialized;
    
} channel_t;

channel_t channels[NUM_CHANNELS];

GLubyte* keymap = NULL;
GLubyte* key_state = NULL;
GLubyte* key_toggle = NULL;
GLubyte* key_press = NULL;

//////////////////////////////////////////////////////////////////////

GLuint program = 0;

GLfloat u_time = 0; // set this to starttime after options
GLfloat u_resolution[3]; // set every frame
GLfloat u_mouse[4] = { -1, -1, -1, -1 }; 
GLfloat u_time_delta = 0;
GLfloat u_date[4]; // set every frame
GLfloat u_pixel_scale[2] = { 1, 1 };

GLint u_frame = 0;

//////////////////////////////////////////////////////////////////////

int window_size[2] = { 640, 360 };
int framebuffer_size[2] = { 0, 0 };

double cur_mouse[2] = { 0, 0 };

int png_frame = 0;

double last_frame_start = 0;
double target_frame_duration = 1.0/60.0;

double speedup = 1.0;
double starttime = 0.0;

int record_frames = 100;

int animating = 1;
int recording = 0;
int need_render = 0;
int single_shot = 0;
int mouse_down = 0;

//////////////////////////////////////////////////////////////////////

const char* shadertoy_id = NULL;
const char* api_key = NULL;

const char* json_input = NULL;
json_t* json_root = NULL;

buffer_t shader_buf = { 0, 0, 0 };
int shader_count = 0;

buffer_t json_buf = { 0, 0, 0 };

//////////////////////////////////////////////////////////////////////

const char* vertex_src[1] = {
    "#version 150\n"
    "in vec2 vertexPosition;\n"
    "void main()\n"
    "{\n"
    "    gl_Position = vec4(vertexPosition, 0.0, 1.0);\n"
    "}\n"
};

enum {
    FRAG_SRC_DECL_SLOT = 0,
    FRAG_SRC_CH0_SLOT = 1,
    FRAG_SRC_CH1_SLOT = 2,
    FRAG_SRC_CH2_SLOT = 3,
    FRAG_SRC_CH3_SLOT = 4,
    FRAG_SRC_MAINIMAGE_SLOT = 5,
    FRAG_SRC_MAIN_SLOT = 6,
    FRAG_SRC_NUM_SLOTS = 7
};

const char* fragment_src[FRAG_SRC_NUM_SLOTS] = {

    "#version 150\n"
    "uniform float iTime; "
    "uniform vec3 iResolution; "
    "uniform vec4 iMouse; "
    "uniform float iTimeDelta; "
    "uniform vec4 iDate; "
    "uniform int iFrame; "
    "uniform vec2 iPixelScale; "
    "float iGlobalTime; "
    "out vec4 fragColor; ",

    "", // iChannel0
    "", // ichannel1
    "", // ichannel2
    "", // ichannel3
    
    "\nvoid mainImage( out vec4 fragColor, in vec2 fragCoord ) {\n"
    "    vec2 uv = fragCoord / iResolution.xy; "
    "    fragColor = vec4(uv,0.5+0.5*sin(iTime),1.0);"
    "}\n",
    
    "void main() {\n"
    "  iGlobalTime = iTime;\n"
    "  mainImage(fragColor, gl_FragCoord.xy/iPixelScale.xy);\n"
    "}\n"
    

};

//////////////////////////////////////////////////////////////////////

const char* get_error_string(GLenum error) {
    switch (error) {
    case GL_NO_ERROR:
        return "no error";
    case GL_INVALID_ENUM:
        return "invalid enum";
    case GL_INVALID_VALUE:
        return "invalid value";
    case GL_INVALID_OPERATION:
        return "invalid operation";
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        return "invalid framebuffer operation";
    case GL_OUT_OF_MEMORY:
        return "out of memory";
    default:
        return "unknown error";
    }
}

//////////////////////////////////////////////////////////////////////

void check_opengl_errors(const char* context) { 
    GLenum error = glGetError();
    if (!context || !*context) { context = "error"; }
    if (error) {
        fprintf(stderr, "%s: %s\n", context, get_error_string(error));
        exit(1);
                                                                         
    }
}

//////////////////////////////////////////////////////////////////////

GLuint make_shader(GLenum type,
                   GLint count,
                   const char** srcs) {

    GLint length[count];

    for (GLint i=0; i<count; ++i) {
        length[i] = strlen(srcs[i]);
    }

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, count, srcs, length);
    glCompileShader(shader);
  
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

    if (!status) {
        char buf[4096];
        glGetShaderInfoLog(shader, sizeof(buf), NULL, buf);
        fprintf(stderr, "error compiling %s shader:\n\n%s\n",
                type == GL_VERTEX_SHADER ? "vertex" : "fragment",
                buf);
        exit(1);
    }

    return shader;
  
}

//////////////////////////////////////////////////////////////////////

void add_uniform(const char* name,
                 const void* src,
                 GLenum type) {

    if (num_uniforms >= MAX_UNIFORMS) {
        fprintf(stderr, "error: maximum # of uniforms exceeded, "
                "increase MAX_UNIFORMS\n");
        exit(1);
    }

    GLuint handle = glGetUniformLocation(program, name);

    uniform_t u = { name, handle, src, 0 };

    switch (type) {
    case GL_FLOAT:
        u.ptr_type = GL_FLOAT;
        u.float_func = glUniform1fv;
        break;
    case GL_FLOAT_VEC2:
        u.ptr_type = GL_FLOAT;
        u.float_func = glUniform2fv;
        break;
    case GL_FLOAT_VEC3:
        u.ptr_type = GL_FLOAT;
        u.float_func = glUniform3fv;
        break;
    case GL_FLOAT_VEC4:
        u.ptr_type = GL_FLOAT;
        u.float_func = glUniform4fv;
        break;
    case GL_INT:
        u.ptr_type = GL_INT;
        u.int_func = glUniform1iv;
        break;
    default:
        fprintf(stderr, "unknown uniform type!\n");
        exit(1);
    }
    
    uinfo[num_uniforms++] = u;
    
}

//////////////////////////////////////////////////////////////////////

void setup_shaders() {

    GLuint vertex_shader = make_shader(GL_VERTEX_SHADER, 1,
                                       vertex_src);

    char sbuf[NUM_CHANNELS][MAX_CHANNEL_DECL_LENGTH];
    
    for (int i=0; i<NUM_CHANNELS; ++i) {

        if (channels[i].ctype != CTYPE_NONE) {

            const char* stype = 0;

            switch (channels[i].ctype) {
            case CTYPE_TEXTURE:
            case CTYPE_KEYBOARD:
                stype = "sampler2D";
                break;
            case CTYPE_CUBEMAP:
                stype = "samplerCube";
                break;
            default:
                fprintf(stderr, "invalid channel type!\n");
                exit(1);
            }

            snprintf(channels[i].name, MAX_CHANNEL_NAME_LENGTH,
                     "iChannel%d", i);

            snprintf(sbuf[i], MAX_CHANNEL_DECL_LENGTH,
                     "uniform %s %s; ",
                     stype, channels[i].name);

            fragment_src[FRAG_SRC_CH0_SLOT+i] = sbuf[i];
            
        }
        
    }

    GLuint fragment_shader = make_shader(GL_FRAGMENT_SHADER,
                                         FRAG_SRC_NUM_SLOTS,
                                         fragment_src);
                                
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    check_opengl_errors("after linking program");

    add_uniform("iTime", &u_time, GL_FLOAT);
    add_uniform("iResolution", u_resolution, GL_FLOAT_VEC3);
    add_uniform("iMouse", u_mouse, GL_FLOAT_VEC4);
    add_uniform("iTimeDelta", &u_time_delta, GL_FLOAT);
    add_uniform("iDate", &u_date, GL_FLOAT_VEC4);
    add_uniform("iFrame", &u_frame, GL_INT);
    add_uniform("iPixelScale", &u_pixel_scale, GL_FLOAT_VEC2);

    check_opengl_errors("after setting up uniforms");

    glUseProgram(program);
    check_opengl_errors("after use program");
    
}

//////////////////////////////////////////////////////////////////////

void setup_array() {

    GLuint vertex_buffer;

    const GLfloat vertices[4][2] = {
        { -1.f, -1.f  },
        {  1.f, -1.f  },
        {  1.f,  1.f  },
        { -1.f,  1.f  }
    };

    GLubyte indices[] = { 0, 1, 2, 0, 2, 3 };
    
    glGenBuffers(1, &vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices),
                 vertices, GL_STATIC_DRAW);
    
    check_opengl_errors("after vertex buffer setup");


    GLuint element_buffer;
    glGenBuffers(1, &element_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices),
                 indices, GL_STATIC_DRAW);
    
    check_opengl_errors("after element buffer setup");

    GLuint vao;
    
    glGenVertexArrays(1, &vao);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer);
    check_opengl_errors("after vao setup");

    GLint vpos_location = glGetAttribLocation(program, "vertexPosition");
    glEnableVertexAttribArray(vpos_location);
    glVertexAttribPointer(vpos_location, 2, GL_FLOAT, GL_FALSE,
                          sizeof(float) * 2, (void*) 0);

    check_opengl_errors("after setting up vertexPosition");

}

//////////////////////////////////////////////////////////////////////

void update_teximage(channel_t* channel) {

    GLenum target;
    int count;

    if (channel->ctype == CTYPE_CUBEMAP) {
        target = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
        count = 6;
    } else {
        target = GL_TEXTURE_2D;
        count = 1;
    }

    GLenum format;

    if (channel->channels == 4) {
        format = GL_RGBA;
    } else {
        format = GL_RGB;
    }

    assert( channel->texture.size == channel->size * count );

    for (int i=0; i<count; ++i) {
    
        if (!channel->initialized) {

            glTexImage2D(target + i, 0, format,
                         channel->width,
                         channel->height, 0,
                         format, GL_UNSIGNED_BYTE,
                         channel->texture.data + i*channel->size);

        } else {

            glTexSubImage2D(target + i, 0,
                            0, 0,
                            channel->width, channel->height,
                            format, GL_UNSIGNED_BYTE,
                            channel->texture.data + i*channel->size);

        }

    }

    if (channel->filter == GL_LINEAR_MIPMAP_LINEAR) {

        glGenerateMipmap(channel->target);
                
    }

    channel->initialized = 1;
    channel->dirty = 0;
    
}

//////////////////////////////////////////////////////////////////////

void setup_textures() {

    for (int i=0; i<4; ++i) {

        if (channels[i].ctype) {

            GLuint channel_loc = glGetUniformLocation(program,
                                                      channels[i].name);
            
            glActiveTexture(GL_TEXTURE0 + i);
            glUniform1i(channel_loc, i);

            glGenTextures(1, &channels[i].tex_id);
            glBindTexture(GL_TEXTURE_2D, channels[i].tex_id);
            
            int mag = channels[i].filter;
            if (mag == GL_LINEAR_MIPMAP_LINEAR) {
                mag = GL_LINEAR;
            }
        
            glTexParameteri(channels[i].target,
                            GL_TEXTURE_MAG_FILTER,
                            mag);
            
            glTexParameteri(channels[i].target,
                            GL_TEXTURE_MIN_FILTER,
                            channels[i].filter);

            glTexParameteri(channels[i].target,
                            GL_TEXTURE_WRAP_S,
                            channels[i].wrap);

            glTexParameteri(channels[i].target,
                            GL_TEXTURE_WRAP_T,
                            channels[i].wrap);

            update_teximage(&channels[i]);

            check_opengl_errors("after dealing with channel");

        }
        
    }

}

//////////////////////////////////////////////////////////////////////

void reset() {

    glfwSetTime(0.0);
    last_frame_start = 0;
    u_time = starttime;
    u_time_delta = 0;
    u_frame = 0;
    u_mouse[0] = u_mouse[1] = u_mouse[2] = u_mouse[3] = -1;

    if (keymap) { memset(keymap, 0, KEYMAP_TOTAL_BYTES); }

    need_render = 1;

}

//////////////////////////////////////////////////////////////////////

void set_uniforms() {

    for (size_t i=0; i<num_uniforms; ++i) {
        const uniform_t* u = uinfo + i;
        switch (u->ptr_type) {
        case GL_FLOAT:
            u->float_func(u->handle, 1, (const GLfloat*)u->src);
            break;
        case GL_INT:
            u->int_func(u->handle, 1, (const GLint*)u->src);
            break;
        default:
            fprintf(stderr, "invalid pointer type in set_uniforms!\n");
            exit(1);
        }
        check_opengl_errors(u->name);
    }

}

//////////////////////////////////////////////////////////////////////

void screenshot() {
    
    glFinish();

    int w = framebuffer_size[0];
    int h = framebuffer_size[1];

    int stride = w*3;

    int align;
    glGetIntegerv(GL_PACK_ALIGNMENT, &align);

    printf("alignment is %d\n", align);

    if (stride % align) {
        stride += align - stride % align;
    }

    unsigned char* screen = (unsigned char*)malloc(h*stride);
  
    if (!screen) {
        fprintf(stderr, "out of memory allocating screen!\n");
        exit(1);
    }
  
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, screen);
  
    char buf[BIG_STRING_LENGTH];
    snprintf(buf, BIG_STRING_LENGTH, "frame%04d.png", png_frame++);
  
    write_png(buf, screen, w, h, stride, 1, u_pixel_scale);
    free(screen);
  
    if (single_shot) {
        single_shot = 0;
    }
  
}


//////////////////////////////////////////////////////////////////////

void render(GLFWwindow* window) {

    glfwGetFramebufferSize(window, framebuffer_size+0, framebuffer_size+1);

    for (int i=0; i<2; ++i) {
        float denom = window_size[i] ? window_size[i] : 1;
        u_pixel_scale[i] = framebuffer_size[i] / denom;
    }

    double frame_start = glfwGetTime();

    if (0) {
        printf("about to render, time=%f, since last=%f, delta=%f, target=%f\n",
               u_time, (frame_start - last_frame_start), u_time_delta,
               target_frame_duration);
    }
    
    struct timeval tv;
    gettimeofday(&tv, NULL);

    time_t time = tv.tv_sec;
    struct tm* ltime = localtime(&time);
  
    u_date[0] = ltime->tm_year + 1900.f;
    u_date[1] = ltime->tm_mon;
    u_date[2] = ltime->tm_mday;
    u_date[3] = ( ((ltime->tm_hour * 60.f) + ltime->tm_min) * 60.f +
                  ltime->tm_sec + tv.tv_usec * 1e-6f );

    u_resolution[0] = framebuffer_size[0] / u_pixel_scale[0];
    u_resolution[1] = framebuffer_size[1] / u_pixel_scale[1];
    u_resolution[2] = 1.f;

    check_opengl_errors("before set uniforms");
    
    set_uniforms();
    check_opengl_errors("after set uniforms");

    for (int i=0; i<NUM_CHANNELS; ++i) {

        if (channels[i].ctype == CTYPE_NONE) { continue; }

        if (channels[i].dirty || channels[i].ctype == CTYPE_KEYBOARD) {

            glBindTexture(channels[i].target, channels[i].tex_id);

            update_teximage(&channels[i]);

        }
        
    }

    if (keymap) {
        memset(key_press, 0, KEYMAP_BYTES_PER_ROW);
    }
    
    glViewport(0, 0, framebuffer_size[0], framebuffer_size[1]);
    glClear(GL_COLOR_BUFFER_BIT);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, (void*)0);
    
    check_opengl_errors("after render");

    if (recording || single_shot) {
        screenshot();
    }
    
    glfwSwapBuffers(window);

    double frame_end = glfwGetTime();
    u_time_delta = frame_end - frame_start;
    
    u_frame += 1;

    if (recording) {
        u_time += target_frame_duration*speedup;
    } else if (animating) {
        u_time += (frame_start - last_frame_start)*speedup;
    }
    
    last_frame_start = frame_start;
    need_render = 0;
    
}




//////////////////////////////////////////////////////////////////////

void new_shader_source() {
    
    char lineno[256];
    snprintf(lineno, 256, "\n#line 0 %d\n", shader_count++);
    buf_append(&shader_buf, lineno, strlen(lineno));

}

//////////////////////////////////////////////////////////////////////

void setup_keyboard(int channel) {

    channel_t* c = channels + channel;

    c->target = GL_TEXTURE_2D;
    c->ctype = CTYPE_KEYBOARD;
    c->width = KEYMAP_BYTES_PER_ROW / 3;
    c->height = KEYMAP_ROWS;
    c->size = KEYMAP_TOTAL_BYTES;

    c->filter = GL_NEAREST;
    c->wrap = GL_CLAMP_TO_EDGE;

    buf_grow(&(c->texture), KEYMAP_TOTAL_BYTES);

    keymap = (unsigned char*)c->texture.data;

    key_state = keymap + 1*KEYMAP_BYTES_PER_ROW;
    key_toggle = keymap + 2*KEYMAP_BYTES_PER_ROW;
    key_press = keymap + 0*KEYMAP_BYTES_PER_ROW;

    memset(keymap, 0, KEYMAP_TOTAL_BYTES);

    c->texture.size = KEYMAP_TOTAL_BYTES;

}

//////////////////////////////////////////////////////////////////////

void load_image(channel_t* channel, const char* src) {

    buffer_t raw = { 0, 0, 0 };
            
    if (strlen(src) > 7 && !memcmp(src, "file://", 7)) {

        const char* filename = src+7;
        buf_read_file(&raw, filename, MAX_FILE_LENGTH);            

    } else {
            
        char url[1024];
        snprintf(url, 1024, "http://www.shadertoy.com%s", src);

        fetch_url(url, &raw);

    }

    const char* extension = get_extension(src);

    if (!strcasecmp(extension, "jpg") ||
        !strcasecmp(extension, "jpeg")) {

        read_jpg(&raw, channel->vflip,
                 &channel->channels,
                 &channel->width, &channel->height, &channel->size,
                 &channel->texture);
                
    } else if (!strcasecmp(extension, "png")) {

        read_png(&raw, channel->vflip,
                 &channel->channels,
                 &channel->width, &channel->height, &channel->size,
                 &channel->texture);

    } else {

        fprintf(stderr, "unrecognized media extension\n");
        exit(1);
                    
    }

    buf_free(&raw);

}

//////////////////////////////////////////////////////////////////////

void load_json() {

    json_root = jsparse(&json_buf);

    json_t* shader = jsobject(json_root, "Shader", JSON_OBJECT);
    json_t* renderpass = jsobject(shader, "renderpass", JSON_ARRAY);

    size_t len = json_array_size(renderpass);
    if (len != 1) {
        fprintf(stderr, "multipass not yet implemented!\n");
        exit(1);
    }

    json_t* image = jsarray(renderpass, len-1, JSON_OBJECT);

    json_t* inputs = jsobject(image, "inputs", JSON_ARRAY);
    int ninputs = json_array_size(inputs);

    for (int i=0; i<ninputs; ++i) {
        
        json_t* input_i = jsarray(inputs, i, JSON_OBJECT);
        
        int cidx = jsobject_integer(input_i, "channel");
        
        if (cidx < 0 || cidx >= NUM_CHANNELS) {
            fprintf(stderr, "invalid channel for input %d\n", i);
            exit(1);
        }

        channel_t* channel = channels + cidx;

        json_t* sampler = jsobject(input_i, "sampler", JSON_OBJECT);

        const enum_info_t filter_enums[] = {
            { "nearest", GL_NEAREST },
            { "mipmap", GL_LINEAR },
            { "linear", GL_LINEAR_MIPMAP_LINEAR },
            { 0, -1 },
        };

        const enum_info_t tf_enums[] = {
            { "true", 1 },
            { "false", 0 },
            { 0, -1 },
        };

        const enum_info_t wrap_enums[] = {
            { "clamp", GL_CLAMP_TO_EDGE },
            { "repeat", GL_REPEAT },
            { 0, -1 },
        };
        
        channel->filter = lookup_enum(filter_enums, 
                                    jsobject_string(sampler, "filter"));

        channel->srgb = lookup_enum(tf_enums,
                                  jsobject_string(sampler, "srgb"));

        channel->vflip = lookup_enum(tf_enums,
                                   jsobject_string(sampler, "vflip"));

        channel->wrap = lookup_enum(wrap_enums,
                                  jsobject_string(sampler, "wrap"));

        const char* ctype = jsobject_string(input_i, "ctype");

        const char* src = jsobject_string(input_i, "src");
        
        if (!strcmp(ctype, "keyboard")) {
            
            setup_keyboard(cidx);
            
        } else if (!strcmp(ctype, "texture")) {
            
            channel->target = GL_TEXTURE_2D;
            channel->ctype = CTYPE_TEXTURE;
            load_image(channel, src);

        } else if (!strcmp(ctype, "cubemap")) {
            
            channel->target = GL_TEXTURE_CUBE_MAP;
            channel->ctype = CTYPE_CUBEMAP;

            const char* dot = strrchr(src, '.');
            if (!dot) { dot = src + strlen(src); }

            int base_len = dot - src;
            int ext_len = strlen(dot);

            if (base_len + ext_len + 2 > 1023) {
                fprintf(stderr, "error: filename too long!\n");
                exit(1);
            }
            
            char base[1024];
            memcpy(base, src, base_len);
            base[base_len] = 0;

            for (int i=0; i<6; ++i) {

                const char* src_i;

                if (i == 0) {
                    src_i = src;
                } else {
                    int ii = i;
                    if (channel->vflip) {
                        if (ii == 2) { ii = 3; }
                        else if (ii == 3) { ii = 2; }
                    }
                    snprintf(base + base_len, 1024-base_len, "_%d%s", ii, dot);
                    src_i = base;
                }

                printf("loading %s\n", src_i);
                load_image(channel, src_i);
                
            }
            
        } else {
            
            fprintf(stderr, "unsupported input type: %s\n", ctype);
            exit(1);
            
        }
        
    }
    
    const char* code_string = jsobject_string(image, "code");

    new_shader_source();
    buf_append(&shader_buf, code_string, strlen(code_string));
    fragment_src[FRAG_SRC_MAINIMAGE_SLOT] = shader_buf.data;
    
}

//////////////////////////////////////////////////////////////////////

void dieusage() {
    
    fprintf(stderr,
            "usage: st_glfw [OPTIONS] (-id SHADERID | BUNDLE.json | SHADER1.glsl [SHADER2.glsl ...])\n"
            "\n"
            "OPTIONS:\n"
            "  -id        SHADERID  Load specified shader from Shadertoy.com\n"
            "  -apikey    KEY       Set Shadertoy.com API key (needed to load shaders)\n"
            "  -geometry  WxH       Initialize window with width W and height H\n"
            "  -speedup   FACTOR    Speed up by this factor\n"
            "  -frames    COUNT     Record COUNT frames to PNG\n"
            "  -duration  TIME      Record TIME seconds to PNG\n"
            "  -fps       FPS       Target FPS for recording\n"
            "  -starttime TIME      Starting value of iTime uniform in seconds\n"
            "  -paused              Start out paused\n"
            "\n"
            );
  
    exit(1);
  
}

//////////////////////////////////////////////////////////////////////

double getdouble(int argc, char** argv, int i) {
    if (i >= argc) {
        fprintf(stderr, "error: expected number for %s\n", argv[i-1]);
        dieusage();
    }
    char* endptr;
    double x = strtod(argv[i], &endptr);
    if (!endptr || *endptr || x <= 0.0) {
        fprintf(stderr, "error: expected positive number for %s\n", argv[i-1]);
        dieusage();
    }
    return x;
}

//////////////////////////////////////////////////////////////////////

long getint(int argc, char** argv, int i) {
    if (i >= argc) {
        fprintf(stderr, "error: expected number for %s\n", argv[i-1]);
        dieusage();
    }
    char* endptr;
    long x = strtol(argv[i], &endptr, 10);
    if (!endptr || *endptr || x < 0) {
        fprintf(stderr, "error: expected non-negative number for %s\n", argv[i-1]);
        dieusage();
    }
    return x;
}

//////////////////////////////////////////////////////////////////////

void get_options(int argc, char** argv) {

    double rduration = 0.0;

    int input_start = argc;
    int force_files = 0;
    
    for (int i=1; i<input_start; ++i) {

        if (!strcmp(argv[i], "-record")) {
            
            recording = 1;
            
        } else if (!strcmp(argv[i], "-geometry")) {
            
            if (i+1 >= argc) {
                fprintf(stderr, "error: expected WxH for -geometry\n");
                dieusage();
            }
            
            int chars;
            
            if (sscanf(argv[i+1], "%dx%d%n", window_size+0, window_size+1, &chars) != 2 ||
                argv[i+1][chars] != '\0') {
                fprintf(stderr, "error: bad format for -geometry\n");
                dieusage();
            }
            
            i += 1;
            
        } else if (!strcmp(argv[i], "-speedup")) {
            
            speedup = getdouble(argc, argv, i+1);
            i += 1;
            
        } else if (!strcmp(argv[i], "-paused")) {
            
            animating = 0;
            
        } else if (!strcmp(argv[i], "-starttime")) {
            
            starttime = getdouble(argc, argv, i+1);
            i += 1;
            
        } else if (!strcmp(argv[i], "-duration")) {
            
            recording = 1;
            rduration = getdouble(argc, argv, i+1);
            i += 1;
            
        } else if (!strcmp(argv[i], "-frames")) {
            
            recording = 1;
            record_frames = getint(argc, argv, i+1);
            i += 1;
            
        } else if (!strcmp(argv[i], "-keyboard")) {

            int key_cidx = getint(argc, argv, i+1);
            
            if (key_cidx < 0 || key_cidx >= 4) {
                fprintf(stderr, "invalid channel for keyboard (must be 0-3)\n");
                exit(1);
            }

            setup_keyboard(key_cidx);

            i += 1;
            
        } else if (!strcmp(argv[i], "-fps")) {
            
            target_frame_duration = 1.0 / getdouble(argc, argv, i+1);
            i += 1;

        } else if (!strcmp(argv[i], "-id")) {

            if (i+1 >= argc) {
                fprintf(stderr, "error: expected id for %s\n", argv[i]);
                dieusage();
            }
            
            shadertoy_id = argv[i+1];
            i += 1;

        } else if (!strcmp(argv[i], "-id")) {

            if (i+1 >= argc) {
                fprintf(stderr, "error: expected id for %s\n", argv[i]);
                dieusage();
            }
            
            shadertoy_id = argv[i+1];
            i += 1;

        } else if (!strcmp(argv[i], "-apikey")) {

            if (i+1 >= argc) {
                fprintf(stderr, "error: expected key for %s\n", argv[i]);
                dieusage();
            }
            
            api_key = argv[i+1];
            i += 1;

        } else if (argv[i][0] == '-' && !force_files) {

            fprintf(stderr, "error: unrecognized switch %s\n", argv[i]);
            dieusage();
            
        } else {
            
            char* tmp = argv[i];
            
            for (int j=i; j<argc-1; ++j) {
                argv[j] = argv[j+1];
            }

            argv[argc-1] = tmp;
            input_start -=1;
            i -= 1;
            
        }


    }

    if (recording && rduration) {
        record_frames = floor(rduration / (target_frame_duration * speedup));
    }

    if (recording) {
        printf("will record for %d frames\n", record_frames);
    }

    if (shadertoy_id) {

        if (input_start != argc) {
            fprintf(stderr, "error: can't specify shadertoy ID and GLSL source!\n");
            exit(1);
        }

        if (!api_key) {
            fprintf(stderr, "error: must set shadertoy API key from command line!\n");
            exit(1);
        }
        
        char url[1024];
        
        snprintf(url, 1024,
                 "http://www.shadertoy.com/api/v1/shaders/%s?key=%s",
                 shadertoy_id, api_key);

        fetch_url(url, &json_buf);

        load_json();
        
    } else {
        
        for (int i=input_start; i<argc; ++i) {

            const char* filename = argv[i];
            const char* extension = get_extension(filename);
            
            if (!strcasecmp(extension, "js") || !strcasecmp(extension, "json")) {
                
                if (input_start != argc - 1) {
                    fprintf(stderr, "error: can't specify more than "
                            "one JSON input!\n");
                    exit(1);
                }
                
                buf_read_file(&json_buf, filename, MAX_FILE_LENGTH);
                load_json();
                
            } else {
                
                new_shader_source();
                buf_read_file(&shader_buf, filename, MAX_FILE_LENGTH);
                fragment_src[FRAG_SRC_MAINIMAGE_SLOT] = shader_buf.data;
                
            }
        }
        
    }

}

//////////////////////////////////////////////////////////////////////

void error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW error: %s\n", description);
}

//////////////////////////////////////////////////////////////////////

void mouse_button_callback(GLFWwindow* window,
                           int button, int action, int mods) {

    if (button == GLFW_MOUSE_BUTTON_LEFT) {

        if (action == GLFW_PRESS) {
            
            u_mouse[0] = cur_mouse[0];
            u_mouse[1] = cur_mouse[1];
            
            u_mouse[2] = cur_mouse[0];
            u_mouse[3] = cur_mouse[1];
            
            mouse_down = 1;
            
        } else {
            
            u_mouse[2] = -u_mouse[2];
            u_mouse[3] = -u_mouse[3];
            
            mouse_down = 0;
            
        }
        
        need_render = 1;
        
    }


}

//////////////////////////////////////////////////////////////////////

void cursor_pos_callback(GLFWwindow* window,
                         double x, double y) {


    cur_mouse[0] = x;
    cur_mouse[1] = window_size[1] - y;

    if (mouse_down) {
        u_mouse[0] = cur_mouse[0];
        u_mouse[1] = cur_mouse[1];
        need_render = 1;
    }
    
}

//////////////////////////////////////////////////////////////////////

void key_callback(GLFWwindow* window, int key,
                  int scancode, int action, int mods) {

    key = toupper(key);

    if (action == GLFW_PRESS) {
        
        if (key == GLFW_KEY_ESCAPE) {
            
            glfwSetWindowShouldClose(window, GL_TRUE);
            
        } else if (key == GLFW_KEY_BACKSPACE ||
                   key == GLFW_KEY_DELETE) {

            reset();
            
        } else if (key == '`' || key == '~') {

            animating = !animating;
            
            if (animating == 1) {
                printf("resumed at %f\n", u_time);
                last_frame_start = 0;
                glfwSetTime(0);
            } else {
                printf("paused at %f\n", u_time);
            }
                
            
        } else if (key == 'S' && (mods & GLFW_MOD_ALT)) {

            printf("saving a screenshot!\n");
            single_shot = 1;

        } else if (key < 256 && keymap) {

            for (int c=0; c<3; ++c) {
                key_press[3*key+c] = 255;
                key_toggle[3*key+c] = ~key_toggle[3*key+c];
                key_state[3*key+c] = 255;
            }

        }
        
    } else if (keymap) { // release

        memset(key_press, 0, KEYMAP_BYTES_PER_ROW);
        if (key_state[3*key]) {
            for (int c=0; c<3; ++c) {
                key_state[3*key+c] = 0;
            }
        }

    }

    need_render = 1;
    
}

//////////////////////////////////////////////////////////////////////

void window_size_callback(GLFWwindow* window,
                          int w, int h) {

    window_size[0] = w;
    window_size[1] = h;

    render(window);
    
}

//////////////////////////////////////////////////////////////////////

GLFWwindow* setup_window() {

    if (!glfwInit()) {
        fprintf(stderr, "Error initializing GLFW!\n");
        exit(1);
    }

    glfwSetErrorCallback(error_callback);

#ifdef __APPLE__    
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);    
#endif

    glfwWindowHint(GLFW_DOUBLEBUFFER, GL_TRUE);
    
    GLFWwindow* window = glfwCreateWindow(window_size[0], window_size[1],
                                          "Shadertoy GLFW", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Error creating window!\n");
        exit(1);
    }

    glfwGetWindowSize(window, window_size+0, window_size+1);

    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetWindowSizeCallback(window, window_size_callback);
    
    glfwMakeContextCurrent(window);
    glewInit();
    glfwSwapInterval(1);
    
    check_opengl_errors("after setting up glfw & glew");

    return window;

}

//////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {
    
    memset(channels, 0, sizeof(channels));

    get_options(argc, argv);

    GLFWwindow* window = setup_window();

    setup_shaders();

    setup_array();

    setup_textures();

    reset();

    while (!glfwWindowShouldClose(window)) {

        if (animating || recording || need_render) {
            render(window);
        }
        
        if (animating || recording) {
            glfwPollEvents();
        } else {
            glfwWaitEvents();
        }
        
        if (recording && record_frames == png_frame) {
            break;
        }
        
    }    

    glfwDestroyWindow(window);
    glfwTerminate();

    buf_free(&json_buf);
    buf_free(&shader_buf);

    for (int i=0; i<NUM_CHANNELS; ++i) {
        buf_free(&channels[i].texture);
    }
    
    if (json_root) { json_decref(json_root); }
    
    return 0;
    
}
