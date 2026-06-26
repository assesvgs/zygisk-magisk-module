cmake_minimum_required(VERSION 3.22)
project(libzygisk C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 17)

file(GLOB_RECURSE XDL_SRC libxdl/_xDL/xdl/src/main/cpp/*.c)
list(FILTER XDL_SRC EXCLUDE REGEX ".*xdl_sample.*")

file(GLOB_RECURSE LSPLT_SRC lsplt/*.cc)

add_library(zygisk SHARED
    src/entry.cpp
    src/ipc_client.cpp
    src/hook.cpp
    src/module.cpp
    ${XDL_SRC}
    ${LSPLT_SRC}
)

target_include_directories(zygisk PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../
    ${CMAKE_CURRENT_SOURCE_DIR}/libxdl/_xDL/xdl/src/main/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libxdl/_xDL/xdl/src/main/cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/lsplt/include
)
target_link_libraries(zygisk log dl)

# xdl uses -Werror with -Weverything, disable for compatibility
target_compile_options(zygisk PRIVATE -Wno-error)
