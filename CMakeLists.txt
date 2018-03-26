cmake_minimum_required(VERSION 2.8)

project(st_glfw)
set(CMAKE_C_FLAGS "-g -Wall")

if(APPLE)
  include_directories(/opt/local/include)
  link_directories(/opt/local/lib)
endif(APPLE)

find_package(glfw3 3 REQUIRED)
find_package(OpenGL REQUIRED)

include(FindPkgConfig)
pkg_check_modules(JANSSON REQUIRED jansson)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(GLEW REQUIRED glew)

add_executable(st_glfw st_glfw.c buffer.c image.c www.c stringutils.c)
target_link_libraries(st_glfw glfw ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES} ${JANSSON_LIBRARIES} ${CURL_LIBRARIES} png jpeg m)
