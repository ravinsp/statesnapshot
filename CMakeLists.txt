cmake_minimum_required(VERSION 3.16)
project(statesnap)

# Check for gold linker for faster linking time.
execute_process(COMMAND ${CMAKE_CXX_COMPILER} -fuse-ld=gold -Wl -ldl,--version OUTPUT_VARIABLE stdout ERROR_QUIET)
if("${stdout}" MATCHES "GNU gold")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-ld=gold")
else()
    message(WARNING "GNU gold linker isn't available, using the default system linker.")
endif()

add_definitions("-std=c++17 -ldl")  #-ldl to support printing stack trace at runtime
set(CMAKE_CXX_FLAGS_DEBUG  "-g")    # ensure debug symbols are added
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY build)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY build)

#find_package(PkgConfig REQUIRED) 
#pkg_check_modules(FUSE_PKG REQUIRED IMPORTED_TARGET fuse3)

add_executable(statesnap
    src/main.cpp
    src/fusefs.cpp
    src/state_monitor.cpp
)
target_link_libraries(statesnap
    libfuse3.so.3
    libsodium.a
    libboost_system.a
    libboost_filesystem.a)

add_executable(restore
    src/restore.cpp
)
target_link_libraries(restore
    libboost_system.a
    libboost_filesystem.a)

# Create docker image from hpcore build output with 'make docker'
# Requires docker to be runnable without 'sudo'
add_custom_target(docker
  COMMAND docker build -t statefs:latest .
)
set_target_properties(docker PROPERTIES EXCLUDE_FROM_ALL TRUE)
add_dependencies(docker
    statesnap
    restore)
