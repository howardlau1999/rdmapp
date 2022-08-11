cmake_minimum_required(VERSION 3.3 FATAL_ERROR)
project(rdmapp)
set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(Threads REQUIRED)
find_package(ibverbs REQUIRED)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

string(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_LENGTH)
add_definitions("-DSOURCE_PATH_LENGTH=${SOURCE_PATH_LENGTH}")

include_directories(include)

option(RDMAPP_BUILD_EXAMPLES "Build examples" ON)

set(RDMAPP_SOURCE_FILES
  src/socket/event_loop.cc
  src/socket/channel.cc
  src/detail/io_buf.cc
  src/socket/tcp_connection.cc
  src/socket/tcp_listener.cc
  src/device.cc
  src/pd.cc
  src/cq.cc
  src/acceptor.cc
  src/qp.cc
  src/srq.cc
  src/connector.cc
  src/cq_poller.cc
  src/executor.cc
  src/mr.cc
)

add_library(rdmapp STATIC ${RDMAPP_SOURCE_FILES})
target_link_libraries(rdmapp ibverbs Threads::Threads)

find_program(iwyu_path NAMES include-what-you-use iwyu)
if (iwyu_path)
  message("-- Using include-what-you-use ${iwyu_path}")
  set_property(TARGET rdmapp PROPERTY CXX_INCLUDE_WHAT_YOU_USE ${iwyu_path})
endif()

if (RDMAPP_BUILD_EXAMPLES)
  set(RDMAPP_EXAMPLES helloworld send_bw)
  foreach(EXAMPLE IN LISTS RDMAPP_EXAMPLES)
    add_executable(${EXAMPLE} examples/${EXAMPLE}.cc)
    target_link_libraries(${EXAMPLE} rdmapp) 
  endforeach()
endif()

include(GNUInstallDirs)
install(TARGETS rdmapp EXPORT rdmapp ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/rdmapp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})