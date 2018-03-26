#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>
#include <png.h>
#include <ctype.h>
#include <assert.h>

#include <jansson.h>
#include <curl/curl.h>

//////////////////////////////////////////////////////////////////////

enum {
    
    MAX_UNIFORMS = 16,
    
    MAX_SAMPLERS = 4,
    MAX_SAMPLER_NAME_LENGTH = 16,
    MAX_SAMPLER_DECL_LENGTH = 64,
    
    BIG_STRING_LENGTH = 1024,
    MAX_PROGRAM_LENGTH = 1024*256,
    MAX_FILE_LENGTH = 1024*1024*1024, // 1GB
    
    KEYMAP_ROWS = 3,
    KEYMAP_BYTES_PER_ROW = 256*3
    
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

typedef struct buffer {
    
    char*  data;
    size_t alloc;
    size_t size;
    
} buffer_t;

typedef struct sampler {

    GLenum type; // e.g. GL_SAMPLER_2D
    char name[MAX_SAMPLER_NAME_LENGTH];

    buffer_t raw_buf;
    buffer_t decoded_buf;
    
} sampler_t;

sampler_t samplers[MAX_SAMPLERS];

//////////////////////////////////////////////////////////////////////

GLubyte keymap[KEYMAP_ROWS * KEYMAP_BYTES_PER_ROW];

GLubyte* key_state = keymap + 1*KEYMAP_BYTES_PER_ROW;
GLubyte* key_toggle = keymap + 2*KEYMAP_BYTES_PER_ROW;
GLubyte* key_press = keymap + 0*KEYMAP_BYTES_PER_ROW;

int key_channel = -1;

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

const char* shadertoy_id = NULL;
const char* api_key = NULL;

const char* json_input = NULL;
json_t* json_root = NULL;

buffer_t shader_buf = { 0, 0, 0 };
int shader_count = 0;

buffer_t json_buf = { 0, 0, 0 };

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

int quick_png(const char* filename,
              const unsigned char* data, 
              size_t ncols,
              size_t nrows,
              size_t rowsz,
              int yflip) {


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
  
    int res_x = base_res * u_pixel_scale[0];
    int res_y = base_res * u_pixel_scale[1];

    png_set_pHYs(png_ptr, info_ptr,
                 res_x, res_y,
                 PNG_RESOLUTION_METER);

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

void screenshot() {
    
    glFinish();

    int w = framebuffer_size[0];
    int h = framebuffer_size[1];

    unsigned char* screen = (unsigned char*)malloc(w*h*3);
  
    if (!screen) {
        fprintf(stderr, "out of memory allocating screen!\n");
        exit(1);
    }
  
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, screen);
  
    char buf[BIG_STRING_LENGTH];
    snprintf(buf, BIG_STRING_LENGTH, "frame%04d.png", png_frame++);
  
    quick_png(buf, screen, w, h, w*3, 1);
    free(screen);
  
    if (single_shot) {
        single_shot = 0;
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

    if (key_channel >= 0) {
        memset(keymap, 0, sizeof(keymap));
    }

    need_render = 1;

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

    if (key_channel >= 0) {
    
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 3, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, keymap);

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
                last_frame_start = 0;
                glfwSetTime(0);
            }
            
        } else if (key == 'S' && (mods & GLFW_MOD_ALT)) {

            printf("saving a screenshot!\n");
            single_shot = 1;

        } else if (key < 256) {

            for (int c=0; c<3; ++c) {
                key_press[3*key+c] = 255;
                key_toggle[3*key+c] = ~key_toggle[3*key+c];
                key_state[3*key+c] = 255;
            }

        }
        
    } else { // release

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

void setup_shaders() {

    GLuint vertex_shader = make_shader(GL_VERTEX_SHADER, 1,
                                       vertex_src);

    char sbuf[MAX_SAMPLERS][MAX_SAMPLER_DECL_LENGTH];
    
    for (int i=0; i<MAX_SAMPLERS; ++i) {

        if (samplers[i].type != 0) {

            const char* stype = 0;

            switch (samplers[i].type) {
            case GL_SAMPLER_2D:
                stype = "sampler2D";
                break;
            default:
                fprintf(stderr, "invalid sampler type!\n");
                exit(1);
            }

            snprintf(samplers[i].name, MAX_SAMPLER_NAME_LENGTH,
                     "iChannel%d", i);

            snprintf(sbuf[i], MAX_SAMPLER_DECL_LENGTH,
                     "uniform %s %s; ",
                     stype, samplers[i].name);

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

void setup_textures() {

    if (key_channel >= 0) {
        
        GLuint channel_loc = glGetUniformLocation(program,
                                                  samplers[key_channel].name);
        
        memset(keymap, 0, sizeof(keymap));
    
        glActiveTexture(GL_TEXTURE0 + 0);
        glUniform1i(channel_loc, 0);

        GLuint tex_id;

        glGenTextures(1, &tex_id);
        glBindTexture(GL_TEXTURE_2D, tex_id);
    
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 3, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, keymap);
    
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        check_opengl_errors("after dealing with iChannel0");
        
    }

}

//////////////////////////////////////////////////////////////////////

void buf_ensure(buffer_t* buf, size_t len) {

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
    

void buf_append(buffer_t* buf, const void* src, size_t len) {

    buf_ensure(buf, len+1);
    
    memcpy(buf->data + buf->size, src, len);
    
    buf->size += len;
    buf->data[buf->size] = 0;

}

void buf_read_file(buffer_t* buf, const char* filename) {

    FILE* fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "error opening %s\n\n", filename);
        exit(1);
    }
    
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);

    if (fsize < 0 || fsize > MAX_FILE_LENGTH) {
        fprintf(stderr, "file exceeds maximum size!\n\n");
        exit(1);
    }
  
    fseek(fp, 0, SEEK_SET);
    
    buf_ensure(buf, fsize+1);

    int nread = fread(buf->data + buf->size, fsize, 1, fp);

    if (nread != 1) {
        fprintf(stderr, "error reading %s\n\n", filename);
        exit(1);
    }

    buf->size += fsize;
    buf->data[buf->size] = 0;

}

//////////////////////////////////////////////////////////////////////

void new_shader_source() {
    
    char lineno[256];
    snprintf(lineno, 256, "\n#line 0 %d\n", shader_count++);
    buf_append(&shader_buf, lineno, strlen(lineno));

}

//////////////////////////////////////////////////////////////////////

size_t write_response(void *ptr, size_t size, size_t nmemb, void * b) {

    buffer_t* buf = (buffer_t*)b;

    buf_append(buf, ptr, size*nmemb);
    
    return size * nmemb;
    
}

//////////////////////////////////////////////////////////////////////

json_t* js_object(const json_t* object,
                              const char* key,
                              int type) {

    json_t* j = json_object_get(object, key);
    
    if (!j) {
        fprintf(stderr, "error: JSON key not found: %s\n", key);
        exit(1);
    }

    if (json_typeof(j) != type) {
        fprintf(stderr, "error: incorrect type for %s in JSON\n", key);
        exit(1);
    }

    return j;
    
}

//////////////////////////////////////////////////////////////////////

json_t* js_array(const json_t* array,
                             int idx,
                             int type) {

    json_t* j = json_array_get(array, idx);
    
    if (!j) {
        fprintf(stderr, "error: array item %d not found in JSON\n", idx);
        exit(1);
    }

    if (json_typeof(j) != type) {
        fprintf(stderr, "error: incorrect type for array item %d in JSON\n", idx);
        exit(1);
    }

    return j;
    
}

//////////////////////////////////////////////////////////////////////

const char* get_extension(const char* filename) {

    const char* extension = strrchr(filename, '.');
    if (!extension) { return ""; }

    return extension+1;

}

//////////////////////////////////////////////////////////////////////

void read_jpg(const buffer_t* raw,
              buffer_t* decoded) {

    fprintf(stderr, "jpeg decoding not supported yet!\n");
    exit(1);

}

//////////////////////////////////////////////////////////////////////

void read_png(const buffer_t* raw,
              buffer_t* decoded) {

    fprintf(stderr, "png decoding not supported yet!\n");
    exit(1);

}

//////////////////////////////////////////////////////////////////////

void fetch_url(const char* url, buffer_t* buf) {

    CURL* curl = curl_easy_init();

    if (!curl) {
        fprintf(stderr, "error initting curl!\n");
        exit(1);
    }


    buf->data = malloc(MAX_PROGRAM_LENGTH);
    buf->alloc = MAX_PROGRAM_LENGTH;
    buf->size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);

    printf("fetching %s...\n", url);
    
    int status = curl_easy_perform(curl);

    if (status != 0) {
        fprintf(stderr, "curl error %s\n", curl_easy_strerror(status));
        exit(1);
    }

    long code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    if (code != 200) {
        fprintf(stderr, "server responded with code %ld\n", code);
        exit(1);
    }

    printf("  ...retrieved data of length %d\n", (int)buf->size);

} 

//////////////////////////////////////////////////////////////////////

void load_json() {

    json_error_t error;

    json_root = json_loadb(json_buf.data, json_buf.size, 0, &error);

    if (!json_root) {
        fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
        exit(1);
    }

    if (!json_is_object(json_root)) {
        fprintf(stderr, "expected JSON root to be object!\n");
        exit(1);
    }

    json_t* shader = js_object(json_root, "Shader", JSON_OBJECT);
    json_t* renderpass = js_object(shader, "renderpass", JSON_ARRAY);

    size_t len = json_array_size(renderpass);
    if (len != 1) {
        fprintf(stderr, "multipass not yet implemented!\n");
        exit(1);
    }

    json_t* image = js_array(renderpass, len-1, JSON_OBJECT);

    json_t* inputs = js_object(image, "inputs", JSON_ARRAY);
    int ninputs = json_array_size(inputs);

    for (int i=0; i<ninputs; ++i) {
        
        json_t* input_i = js_array(inputs, i, JSON_OBJECT);
        
        int channel = json_integer_value(js_object(input_i, "channel", JSON_INTEGER));
        const char* ctype = json_string_value(js_object(input_i, "ctype", JSON_STRING));

        if (!strcmp(ctype, "keyboard")) {
            
            key_channel = channel;
            samplers[key_channel].type = GL_SAMPLER_2D;
            
        } else if (!strcmp(ctype, "texture")) {

            const char* src = json_string_value(js_object(input_i, "src", JSON_STRING));

            buffer_t* raw = &samplers[channel].raw_buf;
            buffer_t* decoded = &samplers[channel].decoded_buf;

            if (strlen(src) > 7 && !memcmp(src, "file://", 7)) {

                const char* filename = src+7;
                buf_read_file(raw, filename);            

            } else {
            
                char url[1024];
                snprintf(url, 1024, "http://www.shadertoy.com%s", src);

                fetch_url(url, raw);

            }

            const char* extension = get_extension(src);

            if (!strcasecmp(extension, "jpg") ||
                !strcasecmp(extension, "jpeg")) {

                read_jpg(raw, decoded);
                
            } else if (!strcasecmp(extension, "png")) {

                read_png(raw, decoded);

            } else {

                fprintf(stderr, "unrecognized media extension\n");
                exit(1);
                    
            }
                
            
            
        } else {
            fprintf(stderr, "unsupported input type: %s\n", ctype);
            exit(1);
        }
        
    }
    
    const char* code_string = json_string_value(js_object(image, "code", JSON_STRING));

    new_shader_source();
    buf_append(&shader_buf, code_string, strlen(code_string));

    fragment_src[FRAG_SRC_MAINIMAGE_SLOT] = shader_buf.data;
    
}


//////////////////////////////////////////////////////////////////////

void load_json_file(const char* filename) {

    buf_read_file(&json_buf, filename);
    load_json();

}


//////////////////////////////////////////////////////////////////////

void load_shadertoy(const char* id) {

    char url[1024];
    
    snprintf(url, 1024,
             "http://www.shadertoy.com/api/v1/shaders/%s?key=%s", id, api_key);

    fetch_url(url, &json_buf);

    load_json();
    
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


void load_shader(const char* filename) {


    new_shader_source();
    buf_read_file(&shader_buf, filename);

    fragment_src[FRAG_SRC_MAINIMAGE_SLOT] = shader_buf.data;

  
}

//////////////////////////////////////////////////////////////////////


int is_json(const char* filename) {

    const char* extension = get_extension(filename);

    return ( !strcasecmp(extension, "js") ||
             !strcasecmp(extension, "json") );

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
            
            key_channel = getint(argc, argv, i+1);
            
            if (key_channel < 0 || key_channel >= 4) {
                fprintf(stderr, "invalid channel for keyboard (must be 0-3)\n");
                exit(1);
            }

            samplers[key_channel].type = GL_SAMPLER_2D;

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
        
        load_shadertoy(shadertoy_id);
        
    } else {
        
        for (int i=input_start; i<argc; ++i) {
            
            if (is_json(argv[i])) {
                
                if (input_start != argc - 1) {
                    fprintf(stderr, "error: can't specify more than one JSON input!\n");
                }
                
                load_json_file(argv[i]);
                
            } else {
                
                load_shader(argv[i]);
                
            }
        }
        
    }

}

//////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {
    
    memset(samplers, 0, sizeof(samplers));

    curl_global_init(CURL_GLOBAL_ALL);

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
    if (shader_buf.data) { free(shader_buf.data); }
    if (json_root) { json_decref(json_root); }
    if (json_buf.data) { free(json_buf.data); }
    
    return 0;
    
}
