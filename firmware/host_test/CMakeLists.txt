cmake_minimum_required(VERSION 3.20)
project(tlsr_layout_host_test C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Werror -Wno-unused-parameter)

set(FW ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(host_test
    main.c
    host_canvas.c
    flash_stub.c
    measure_host.c
    fonts_host.c
    ${FW}/src/dsl/arena.c
    ${FW}/src/dsl/parser.c
    ${FW}/src/engine/engine.c
    ${FW}/src/engine/measure.c
    ${FW}/src/proto/crc32.c
    ${FW}/src/proto/uzlib/adler32.c
    ${FW}/src/proto/uzlib/crc32.c
    ${FW}/src/proto/uzlib/defl_static.c
    ${FW}/src/proto/uzlib/tinfgzip.c
    ${FW}/src/proto/uzlib/tinflate.c
    ${FW}/src/proto/uzlib/tinfzlib.c
    ${FW}/src/render/palette.c
    ${FW}/src/render/icons.c
    ${FW}/src/render/primitives.c
    ${FW}/src/render/placeholder.c
    ${FW}/src/render/paint.c
    ${FW}/src/store/slot.c
)

target_include_directories(host_test PRIVATE
    ${FW}/src
    ${FW}/src/dsl
    ${FW}/src/engine
    ${FW}/src/proto/uzlib
    ${FW}/src/render
    ${FW}/src/store
)

target_compile_definitions(host_test PRIVATE TLSR_LAYOUT_HOST_BUILD=1)
