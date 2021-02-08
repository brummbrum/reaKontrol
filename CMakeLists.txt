cmake_minimum_required (VERSION 3.13.3)

project(reakontrol)

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    message("Install prefix: initialized to default > ${CMAKE_INSTALL_PREFIX}")
    if(WIN32)
        message("Install prefix: Setting install prefix to Home:$ENV{APPDATA}")
        set (CMAKE_INSTALL_PREFIX "$ENV{APPDATA}/" CACHE PATH "default install path" FORCE ) 
    elseif (APPLE)
        message("Install prefix: Setting install prefix to Home:$ENV{HOME}/Library")
        set (CMAKE_INSTALL_PREFIX "$ENV{HOME}/Library" CACHE PATH "default install path" FORCE ) 
set(CMAKE_OSX_ARCHITECTURES x86_64)
    endif()
endif()
message("Install prefix: ${CMAKE_INSTALL_PREFIX}")

if(CMAKE_BUILD_TYPE MATCHES DEBUG)
    message("debug mode")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0") 
else(CMAKE_BUILD_TYPE MATCHES DEBUG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DRELEASEVERSION")
endif(CMAKE_BUILD_TYPE MATCHES DEBUG) 

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(src)
