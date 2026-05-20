CMAKE_MINIMUM_REQUIRED(VERSION 3.28.1)

# We will always be building Release in order to enable all needed optimizations
# Debug symbols will be still present in the ELF file
# SET(CMAKE_BUILD_TYPE Release)

# Prepare the toolchain for Telink 8251/8258 microcontroller
INCLUDE(cmake/TelinkSDK.cmake)

PROJECT(TLSR_PRICE_TAG C)
ENABLE_LANGUAGE(ASM)

ADD_SUBDIRECTORY(src)

