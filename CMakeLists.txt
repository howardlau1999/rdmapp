cmake_minimum_required(VERSION 3.1)
project(rdmapp)

find_package(Threads)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(CMAKE_CXX_STANDARD 20)

string(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_LENGTH)
add_definitions("-DSOURCE_PATH_LENGTH=${SOURCE_PATH_LENGTH}")

include_directories(include)

option(RDMAPP_BUILD_EXAMPLES "Build examples" ON)

set(RDMAPP_SOURCE_FILES
  src/detail/socket.cc
  src/device.cc
  src/pd.cc
  src/cq.cc
  src/acceptor.cc
  src/qp.cc
  src/srq.cc
  src/cq_poller.cc
  src/executor.cc
  src/mr.cc
)

add_library(rdmapp STATIC ${RDMAPP_SOURCE_FILES})
target_link_libraries(rdmapp ibverbs)

if (RDMAPP_BUILD_EXAMPLES)
  add_executable(helloworld examples/helloworld.cc)
  target_link_libraries(helloworld rdmapp)
endif()

include(GNUInstallDirs)
install(TARGETS rdmapp EXPORT rdmapp ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/rdmapp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})