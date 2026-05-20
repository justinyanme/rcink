SET(TARGET TLSR-PRICE-TAG)

INCLUDE(${CMAKE_SOURCE_DIR}/cmake/util.cmake)

IF (NOT MANUFACTURER_CODE)
    SET(MANUFACTURER_CODE 0)
ENDIF ()

SET(EPD_PAD_TOP 8 CACHE STRING "Vertical pad (rows) added before the 2.13\" framebuffer. Use 8 for FPC-7528B, 0 for FPC-A002.")
IF (NOT EPD_PAD_TOP MATCHES "^-?[0-9]+$")
    MESSAGE(FATAL_ERROR "EPD_PAD_TOP must be an integer; got '${EPD_PAD_TOP}'. Pass -DEPD_PAD_TOP=<rows> to cmake.")
ENDIF ()


ADD_DEFINITIONS(
        -DEND_DEVICE=1
        -DMCU_CORE_${TELINK_PLATFORM}=1
        -DMCU_STARTUP_${TELINK_PLATFORM}=1
        -DMANUFACTURER_CODE=${MANUFACTURER_CODE}
        -DEPD_PAD_TOP=${EPD_PAD_TOP}
)

set(EPD_REFRESH_STRATEGY "SMART" CACHE STRING "EPD refresh strategy (SMART or FORCE_FULL)")
string(TOUPPER "${EPD_REFRESH_STRATEGY}" EPD_REFRESH_STRATEGY_UPPER)
if(EPD_REFRESH_STRATEGY_UPPER STREQUAL "SMART")
    add_definitions(-DEPD_REFRESH_STRATEGY_SMART)
elseif(EPD_REFRESH_STRATEGY_UPPER STREQUAL "FORCE_FULL")
    add_definitions(-DEPD_REFRESH_STRATEGY_FORCE_FULL)
else()
    message(FATAL_ERROR "Unknown EPD_REFRESH_STRATEGY='${EPD_REFRESH_STRATEGY}'. Use SMART or FORCE_FULL.")
endif()

set(QUOTE_FONT_FORCE_STACK "AUTO" CACHE STRING "Force a single quote font stack (AUTO, NOTO, CLASSIC, WENQUANYI)")
string(TOUPPER "${QUOTE_FONT_FORCE_STACK}" QUOTE_FONT_FORCE_STACK_UPPER)
if(QUOTE_FONT_FORCE_STACK_UPPER STREQUAL "AUTO")
    add_definitions(-DQUOTE_FONT_FORCE_STACK_AUTO)
elseif(QUOTE_FONT_FORCE_STACK_UPPER STREQUAL "NOTO")
    add_definitions(-DQUOTE_FONT_FORCE_STACK_NOTO)
elseif(QUOTE_FONT_FORCE_STACK_UPPER STREQUAL "CLASSIC")
    add_definitions(-DQUOTE_FONT_FORCE_STACK_CLASSIC)
elseif(QUOTE_FONT_FORCE_STACK_UPPER STREQUAL "WENQUANYI")
    add_definitions(-DQUOTE_FONT_FORCE_STACK_WENQUANYI)
else()
    message(FATAL_ERROR "Unknown QUOTE_FONT_FORCE_STACK='${QUOTE_FONT_FORCE_STACK}'. Use AUTO, NOTO, CLASSIC, or WENQUANYI.")
endif()

option(ENABLE_QUOTE_FONT_NOTO "Include the Noto quote font stack" ON)
option(ENABLE_QUOTE_FONT_CLASSIC "Include the Classic quote font stack" ON)
option(ENABLE_QUOTE_FONT_WENQUANYI "Include the WenQuanYi quote font stack" ON)

if(ENABLE_QUOTE_FONT_NOTO)
    add_definitions(-DQUOTE_FONT_ENABLE_NOTO=1)
else()
    add_definitions(-DQUOTE_FONT_ENABLE_NOTO=0)
endif()

if(ENABLE_QUOTE_FONT_CLASSIC)
    add_definitions(-DQUOTE_FONT_ENABLE_CLASSIC=1)
else()
    add_definitions(-DQUOTE_FONT_ENABLE_CLASSIC=0)
endif()

if(ENABLE_QUOTE_FONT_WENQUANYI)
    add_definitions(-DQUOTE_FONT_ENABLE_WENQUANYI=1)
else()
    add_definitions(-DQUOTE_FONT_ENABLE_WENQUANYI=0)
endif()

set(QUOTE_ROTATE_INTERVAL_SECONDS "" CACHE STRING "Seconds between quote rotations (blank = default: 300s debug / 3600s release)")
if(QUOTE_ROTATE_INTERVAL_SECONDS)
    add_definitions(-DQUOTE_ROTATE_INTERVAL_SECONDS=${QUOTE_ROTATE_INTERVAL_SECONDS})
endif()

set(RUNTIME_QUOTES_FLASH_ADDR "0x73000" CACHE STRING "Flash sector address for runtime BLE quote packs")
if(NOT RUNTIME_QUOTES_FLASH_ADDR MATCHES "^0x[0-9A-Fa-f]+$|^[0-9]+$")
    message(FATAL_ERROR "RUNTIME_QUOTES_FLASH_ADDR must be an integer literal; got '${RUNTIME_QUOTES_FLASH_ADDR}'")
endif()
add_definitions(-DRUNTIME_QUOTES_FLASH_ADDR=${RUNTIME_QUOTES_FLASH_ADDR})

if(NOT (ENABLE_QUOTE_FONT_NOTO OR ENABLE_QUOTE_FONT_CLASSIC OR ENABLE_QUOTE_FONT_WENQUANYI))
    message(FATAL_ERROR "At least one quote font stack must remain enabled.")
endif()

if(ENABLE_QUOTE_FONT_WENQUANYI)
    set(WENQUANYI_FONT_SIZES "9" CACHE STRING "Semicolon or comma separated WenQuanYi sizes to compile (choices: 9,10,12,16)")
    string(REPLACE "," ";" _WQY_SIZES_RAW "${WENQUANYI_FONT_SIZES}")
    string(REPLACE " " "" _WQY_SIZES_RAW "${_WQY_SIZES_RAW}")
    set(_WQY_VALID_SIZES 9 10 12 16)
    set(_WQY_SIZES_LIST ${_WQY_SIZES_RAW})
    list(REMOVE_ITEM _WQY_SIZES_LIST "")
    if(NOT _WQY_SIZES_LIST)
        message(FATAL_ERROR "WENQUANYI_FONT_SIZES must include at least one entry (subset of 9,10,12,16)")
    endif()
    foreach(size IN LISTS _WQY_SIZES_LIST)
        list(FIND _WQY_VALID_SIZES "${size}" _idx)
        if(_idx EQUAL -1)
            message(FATAL_ERROR "Invalid WenQuanYi font size '${size}'. Choose from 9,10,12,16 or provide a ';' separated subset.")
        endif()
    endforeach()
    foreach(valid_size IN LISTS _WQY_VALID_SIZES)
        list(FIND _WQY_SIZES_LIST "${valid_size}" _idx)
        if(NOT _idx EQUAL -1)
            add_definitions(-DQUOTE_FONT_HAS_WQY_${valid_size})
        endif()
    endforeach()
endif()

# set build timestamp
EXECUTE_PROCESS(COMMAND date +%s OUTPUT_VARIABLE BUILD_UNIX_TIMESTAMP OUTPUT_STRIP_TRAILING_WHITESPACE)
SET_BUILD_TIMESTAMP(BUILD_YEAR "%Y")
SET_BUILD_TIMESTAMP(BUILD_MONTH "%m")
SET_BUILD_TIMESTAMP(BUILD_DAY "%d")
SET_BUILD_TIMESTAMP(BUILD_WEEK_DAY "%w") # The day of the current week. 0 is Sunday. (0-6)
SET_BUILD_TIMESTAMP(BUILD_HOUR "%H")
SET_BUILD_TIMESTAMP(BUILD_MIN "%M")
SET_BUILD_TIMESTAMP(BUILD_SEC "%S")
CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/cmake/time_template.h.in ${CMAKE_BINARY_DIR}/generated/time_config.h)
INCLUDE_DIRECTORIES(${CMAKE_BINARY_DIR}/generated)

################################
FILE(GLOB_RECURSE TELINK_SDK_SRC
        # boot
        ${SDK_PREFIX}/boot/B85/cstartup_825x.S
        ${SDK_PREFIX}/div_mod.S
        # application
        ${SDK_PREFIX}/application/print/*.c
        # common
        ${SDK_PREFIX}/common/*.c
        # driver
        ${SDK_PREFIX}/drivers/8258/*.c
        ${SDK_PREFIX}/drivers/8258/**/*.c
        # vendor common
        ${SDK_PREFIX}/vendor/common/*.c
)

#FILE(REMOVE TELINK_SDK_SRC ${SDK_PREFIX}/vendor/common/battery_check.c)

################################
# Telink device

INCLUDE_DIRECTORIES(BEFORE
        ${PROJECT_SOURCE_DIR}/src/include_override
)
INCLUDE_DIRECTORIES(
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/vendor

        ${SDK_PREFIX}
        ${SDK_PREFIX}/vendor/common
        ${SDK_PREFIX}/common
        ${SDK_PREFIX}/drivers/8258
)

LINK_DIRECTORIES(
        ${SDK_PREFIX}/proj_lib
)

SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T${SDK_PREFIX}/boot.link")

FILE(GLOB_RECURSE SOURCES1 *.c *.cpp *.S)

option(ENABLE_EPD_SCENE_TIME "Include the clock scene" ON)
option(ENABLE_EPD_SCENE_LIST "Include the list scene" ON)

if(ENABLE_EPD_SCENE_TIME)
    add_definitions(-DEPD_HAS_SCENE_TIME=1)
else()
    add_definitions(-DEPD_HAS_SCENE_TIME=0)
    list(FILTER SOURCES1 EXCLUDE REGEX "epd/epd\\+time\\.c$")
endif()

if(ENABLE_EPD_SCENE_LIST)
    add_definitions(-DEPD_HAS_SCENE_LIST=1)
else()
    add_definitions(-DEPD_HAS_SCENE_LIST=0)
    list(FILTER SOURCES1 EXCLUDE REGEX "epd/epd\\+list\\.c$")
endif()

# NOTE: maybe could split to separate cmake files
# vendor
## one bit display
#file(GLOB OBD_SRC ${PROJECT_SOURCE_DIR}/vendor/OneBitDisplay/src/*.*)
## TIFF_G4
#file(GLOB TIFF_SRC ${PROJECT_SOURCE_DIR}/vendor/TIFF_G4/src/*.*)

SET(SOURCES
        ${SOURCES1} ${TELINK_SDK_SRC}
        #        ${OBD_SRC}
        #        ${TIFF_SRC}
        ${PROJECT_SOURCE_DIR}/vendor/vendor.c
)
MESSAGE(STATUS "SOURCES: ${SOURCES}")

ADD_EXECUTABLE(${TARGET} ${SOURCES})
# Link the target with the libraries
TARGET_LINK_LIBRARIES(${TARGET}
        -Wl,--start-group
        lt_825x
        lt_general_stack
        -Wl,--end-group
)

SET_PROPERTY(TARGET ${TARGET} PROPERTY C_STANDARD 23)

ADD_BIN_TARGET(${TARGET} ${PROJECT_SOURCE_DIR}/tools)
ADD_OTA_TARGET(${TARGET} ${PROJECT_SOURCE_DIR}/tools)

# this could be override by command line:
# cmake -DQUOTES_FILE=goofish_5.txt ..
# Set quotes file variable
if(NOT DEFINED QUOTES_FILE OR QUOTES_FILE STREQUAL "")
    set(QUOTES_FILE "rocry.txt")
endif()
set(QUOTES_FILE_PATH "${CMAKE_SOURCE_DIR}/cmake/quotes/${QUOTES_FILE}")
if(NOT EXISTS "${QUOTES_FILE_PATH}")
    message(FATAL_ERROR "Quotes file ${QUOTES_FILE_PATH} not found. Pass -DQUOTES_FILE=<name>.txt for a file under cmake/quotes")
endif()

# Generate quotes header
ADD_CUSTOM_COMMAND(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/quotes.h
    COMMAND python3 ${CMAKE_SOURCE_DIR}/cmake/generate_quotes.py
            ${QUOTES_FILE_PATH}
            ${CMAKE_BINARY_DIR}/generated/quotes.h
    DEPENDS ${QUOTES_FILE_PATH} ${CMAKE_SOURCE_DIR}/cmake/generate_quotes.py
    COMMENT "Generating quotes.h"
)
ADD_CUSTOM_TARGET(generate_quotes_header
    DEPENDS ${CMAKE_BINARY_DIR}/generated/quotes.h
)
ADD_DEPENDENCIES(${TARGET} generate_quotes_header)
