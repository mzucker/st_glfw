cmake_minimum_required(VERSION 2.8)

project(st_glfw)
set(CMAKE_C_FLAGS "-g -Wall")

if(APPLE)
  include_directories(/opt/local/include)
  link_directories(/opt/local/lib)
endif(APPLE)

find_package(glfw3 3.2 REQUIRED)
find_package(glew REQUIRED)
find_package(OpenGL REQUIRED)

include(FindPkgConfig)
pkg_check_modules(JANSSON REQUIRED jansson)
pkg_check_modules(CURL REQUIRED libcurl)

add_executable(st_glfw st_glfw.c)
target_link_libraries(st_glfw glfw glew png ${OPENGL_LIBRARIES} ${JANSSON_LIBRARIES} ${CURL_LIBRARIES})
