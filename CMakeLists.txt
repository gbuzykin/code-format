cmake_minimum_required(VERSION 3.12)
project(code-format)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions($<$<CONFIG:Debug>:_DEBUG_REDUCED_BUFFERS>)

if(MSVC)
  add_compile_options(/Zc:__cplusplus)
else()
  add_compile_definitions($<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=2>)
  add_compile_options($<$<CONFIG:Debug>:-fsanitize=address,undefined,leak>)
  add_link_options($<$<CONFIG:Debug>:-fsanitize=address,undefined,leak>)
  add_compile_options(-Wall -pedantic -fno-rtti)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options($<$<CONFIG:Debug>:-fstandalone-debug>)
    add_link_options($<$<CONFIG:Debug>:-shared-libsan>)
  endif()
endif()

file(READ "VERSION" VERSION)
add_compile_definitions(VERSION=${VERSION})

###############################################################################
## file globbing ##############################################################
###############################################################################

if(WIN32)
  set(uxs_platform_dir    uxs/platform/win)
elseif(UNIX)
  set(uxs_platform_dir    uxs/platform/posix)
endif()
file(GLOB_RECURSE includes   include/*.h)
file(GLOB_RECURSE sources    src/*.h;src/*.cpp)
file(GLOB_RECURSE uxs_includes    uxs/include/*.h)
file(GLOB_RECURSE uxs_sources     uxs/src/*.h;uxs/src/*.cpp)
file(GLOB_RECURSE uxs_platform_sources    ${uxs_platform_dir}/src/*.h;${uxs_platform_dir}/src/*.cpp)

###############################################################################
## target definitions #########################################################
###############################################################################

add_executable(code-format
  .clang-format
  ${uxs_includes}
  ${uxs_sources}
  ${uxs_platform_sources}
  ${includes}
  ${sources}
)

target_include_directories(code-format PUBLIC uxs/include include)
