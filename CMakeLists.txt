cmake_minimum_required(VERSION 3.10)
project(building-your-own-lisp)

option(ENABLE_ASAN "AddressSanitizer" OFF)

if (APPLE)
  set(CMAKE_OSX_SYSROOT /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk)
endif()


set(CMAKE_C_FLAGS "-Werror -Wall -g")

if(ENABLE_ASAN)
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
endif()

message(WARNING ${CMAKE_C_FLAGS})

include_directories("src/" "mpc/" "/usr/local/include")

add_executable(main src/parsing.c mpc/mpc.c)
target_link_libraries(main edit m)
