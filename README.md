# st_glfw
Standalone hosting environment for Shadertoy using GLFW

## Building

Required packages:

  - glfw3 
  - glew
  - libpng
  - libjpeg
  - libcurl
  - jansson
  - cmake
  
 To compile
 
     cd /path/to/st_glfw
     mkdir build
     cd build
     cmake -DCMAKE_BUILD_TYPE=Release ..
     make
