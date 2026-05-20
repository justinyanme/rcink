SET(TARGET TLSR-LAYOUT)

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

SET(CMAKE_EXE_LINKER_FLAGS
        "${CMAKE_EXE_LINKER_FLAGS} -T${SDK_PREFIX}/boot.link -Wl,-u,parse_layout -Wl,-u,run_layout -Wl,-u,paint_layout -Wl,-u,draw_text -Wl,-u,draw_hbar -Wl,-u,palette_from_token")

FILE(GLOB_RECURSE SOURCES1 CONFIGURE_DEPENDS *.c *.cpp *.S)

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
