cmake_minimum_required(VERSION 3.10)
project(canfetti)

# -std=gnu++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS YES)

find_package(catkin)
if(catkin_FOUND)
  catkin_package(
    INCLUDE_DIRS include
    LIBRARIES canfetti)
endif()

set(CORE_SRC
  src/CanDevice.cpp
  src/LocalNode.cpp
  src/ObjDict.cpp
  src/OdData.cpp
  src/services/Emcy.cpp
  src/services/Nmt.cpp
  src/services/Pdo.cpp
  src/services/sdo/Client.cpp
  src/services/sdo/Protocol.cpp
  src/services/sdo/Server.cpp
  src/services/sdo/ServerBlockMode.cpp
  src/services/Sdo.cpp)

add_library(canfetti SHARED
  ${CORE_SRC}
  src/platform/linux/LinuxCo.cpp)
target_include_directories(canfetti PUBLIC
  include
  include/platform/linux)
target_compile_options(canfetti PRIVATE -g)
target_link_libraries(canfetti PUBLIC pthread)

if(NOT catkin_FOUND)
  add_executable(canfetti_unittest
    ${CORE_SRC}
    src/platform/unittest/test.cpp
    src/platform/unittest/test-od.cpp
    src/platform/unittest/test-client.cpp
    src/platform/unittest/test-callbacks.cpp
    )
  target_include_directories(canfetti_unittest PUBLIC
    include
    include/platform/unittest)
  target_link_libraries(canfetti_unittest PRIVATE pthread gtest gmock gtest_main)
  target_compile_options(canfetti_unittest PRIVATE -g -O0)

  add_executable(canfetti_threadtest
    src/platform/linux/test/threads.cpp
    )
  target_compile_options(canfetti_threadtest PRIVATE -g -Og)
  target_link_libraries(canfetti_threadtest PRIVATE canfetti)

  add_executable(canfetti_blocktest
    src/platform/linux/test/blockmode.cpp
    )
  target_link_libraries(canfetti_blocktest PRIVATE canfetti)

  add_executable(canfetti_vectortest
    src/platform/linux/test/vector.cpp
    )
  target_link_libraries(canfetti_vectortest PRIVATE canfetti)

  add_executable(canfetti_generationtest
    src/platform/linux/test/generation.cpp
    )
  target_link_libraries(canfetti_generationtest PRIVATE canfetti)
endif()

if(catkin_FOUND)
  install(
    DIRECTORY
      include/canfetti
      # Dump the Linux platform headers into the same directory because Catkin
      # is unreasonable about exporting multiple include dirs.
      include/platform/linux/canfetti
    DESTINATION ${CATKIN_GLOBAL_INCLUDE_DESTINATION})
  install(
    TARGETS canfetti
    ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
    LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
    RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION})
endif()
