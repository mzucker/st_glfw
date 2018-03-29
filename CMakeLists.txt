cmake_minimum_required(VERSION 2.8)

project(st_glfw)
set(CMAKE_C_FLAGS "-g -Wall")
set(USE_CURL true CACHE BOOL "Use curl library for downloading from Shadertoy")
set(USE_GLEW true CACHE BOOL "Use GLEW extension loader library")

if(APPLE)
  include_directories(/opt/local/include)
  link_directories(/opt/local/lib)
endif(APPLE)

find_package(glfw3 3 REQUIRED)
find_package(OpenGL REQUIRED)

include(FindPkgConfig)
pkg_check_modules(JANSSON REQUIRED jansson)

if (USE_CURL)
  pkg_check_modules(CURL REQUIRED libcurl)
endif()

if (USE_GLEW)
  pkg_check_modules(GLEW glew)
endif()

if (GLEW_FOUND)
  add_definitions(-DST_GLFW_USE_GLEW)
endif(GLEW_FOUND)

if (CURL_FOUND)
  add_definitions(-DST_GLFW_USE_CURL)
endif(CURL_FOUND)

add_executable(st_glfw st_glfw.c buffer.c image.c require.c stringutils.c www.c)
target_link_libraries(st_glfw glfw ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES} ${JANSSON_LIBRARIES} ${CURL_LIBRARIES} png jpeg m)
