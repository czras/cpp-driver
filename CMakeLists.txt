cmake_minimum_required(VERSION 2.6.4)

include(FindPackageHandleStandardArgs)

set(PROJECT_NAME_STR cassandra)
set(PROJECT_LIB_NAME ${PROJECT_NAME_STR})
set(PROJECT_LIB_NAME_STATIC "${PROJECT_LIB_NAME}_static")

# TODO: Figure out Mac OS X rpath
if(POLICY CMP0042)
  cmake_policy(SET CMP0042 OLD)
endif()

project(${PROJECT_NAME_STR} C CXX)
set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.beta5)

set(CASS_DRIVER_PROJECT_NAME ${PROJECT_NAME_STR})

if(32BIT)
  set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS FALSE)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32")
endif()

set(CASS_MINIMUM_BOOST_VERSION 1.55.0)

option(CASS_INSTALL_HEADER "Install header file" ON)
option(CASS_USE_EXTERNAL_BOOST "Use external verison of Boost" OFF)
option(CASS_BUILD_STATIC "Build static library" OFF)
option(CASS_BUILD_EXAMPLES "Build examples" ON)
option(CASS_BUILD_TESTS "Build tests" OFF)
option(CASS_USE_OPENSSL "Use OpenSSL" ON)
option(CASS_USE_TCMALLOC "Use tcmalloc" OFF)

if(CASS_BUILD_TESTS)
  set(CASS_USE_EXTERNAL_BOOST ON)
  set(CASS_BUILD_STATIC ON)
endif()

# Build up the include paths
set(PROJECT_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(INCLUDES ${INCLUDES} ${PROJECT_INCLUDE_DIR})
set(INCLUDES ${INCLUDES} ${PROJECT_SOURCE_DIR}/src)
set(INCLUDES ${INCLUDES} ${PROJECT_SOURCE_DIR}/src/third_party/rapidjson)

#-------------------
# Dependencies
#-------------------

# libuv
set(_LIBUV_ROOT_HINTS ${LIBUV_ROOT_DIR} ENV LIBUV_ROOT_DIR)

if(WIN32)
  set(_LIBUV_ROOT_PATHS "${PROJECT_SOURCE_DIR}/lib/libuv/")
  set(_LIBUV_ROOT_HINTS_AND_PATHS HINTS ${_LIBUV_ROOT_HINTS} PATHS ${_LIBUV_ROOT_PATHS})
else()
  set(_LIBUV_ROOT_PATHS "/usr/" "/usr/local/")
  set(_LIBUV_ROOT_HINTS_AND_PATHS HINTS ${_LIBUV_ROOT_HINTS} PATHS ${_LIBUV_ROOT_PATHS})
endif()

find_path(LIBUV_INCLUDE_DIR
  NAMES uv.h
  HINTS ${_LIBUV_INCLUDEDIR} ${_LIBUV_ROOT_HINTS_AND_PATHS}
  PATH_SUFFIXES include)

find_library(LIBUV_LIBRARY
  NAMES uv libuv
  HINTS ${_LIBUV_LIBDIR} ${_LIBUV_ROOT_HINTS_AND_PATHS}
  PATH_SUFFIXES lib)

set(LIBUV_LIBRARIES ${LIBUV_LIBRARY})

find_package_handle_standard_args(Libuv "Could NOT find libuv, try to set the path to the libuv root folder in the system variable LIBUV_ROOT_DIR"
  LIBUV_LIBRARIES
  LIBUV_INCLUDE_DIR)

set(INCLUDES ${INCLUDES} ${LIBUV_INCLUDE_DIR})
set(LIBS ${LIBS} ${LIBUV_LIBRARIES})

# boost

if(CASS_USE_EXTERNAL_BOOST)
  if(WIN32)
    set(BOOST_ROOT "${PROJECT_SOURCE_DIR}/lib/boost/")
  endif()

  find_package(Boost ${CASS_MINIMUM_BOOST_VERSION} COMPONENTS atomic chrono date_time log random system thread unit_test_framework)
endif()

# tcmalloc

if(CASS_USE_TCMALLOC)
  find_library(GPERFTOOLS_TCMALLOC
      NAMES tcmalloc
      HINTS ${Gperftools_ROOT_DIR}/lib)

  set(GPERFTOOLS_LIBRARIES ${GPERFTOOLS_TCMALLOC})

  find_package_handle_standard_args(Gperftools "Could NOT find gperftools, try to set the path to the gperftools root folder in the system variable Gperftools_ROOT_DIR"
    GPERFTOOLS_LIBRARIES)

  set(LIBS ${LIBS} ${GPERFTOOLS_LIBRARIES})
endif()

# openssl

if(CASS_USE_OPENSSL)
  if(WIN32)
    set(_OPENSSL_VERSION "1.0.1h")
    set(OPENSSL_ROOT_DIR "${PROJECT_SOURCE_DIR}/lib/openssl/")
  endif()
  find_package(OpenSSL REQUIRED)
  set(LIBS ${LIBS} ${OPENSSL_LIBRARIES})
  set(INCLUDES ${INCLUDES} ${OPENSSL_INCLUDE_DIR} )
endif()

if(WIN32)
  set(LIBS ${LIBS} iphlpapi psapi wsock32 ws2_32)
endif()

#-------------------
# Compiler flags
#-------------------

if("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
endif()

set(MULTICORE_CORES_NUMBER "3" CACHE STRING "Number of cores (for multicore compilation)")
option(MULTICORE_COMPILATION "Enable multicore compilation" OFF)

if(NOT WIN32)
  set(TEST_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBOOST_THREAD_USES_MOVE -DBOOST_NO_CXX11_RVALUE_REFERENCES")
endif()

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  if(MULTICORE_COMPILATION)
    add_definitions("/MP${MULTICORE_CORES_NUMBER}")
  endif()

  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /NODEFAULTLIB:LIBCMT")

  # On Visual C++ -pedantic flag is not used,
  # -fPIC is not used on Windows platform (all DLLs are
  # relocable), -Wall generates about 30k stupid warnings
  # that can hide useful ones.

  # TODO(mpenick): Fix these "possible loss of data" warnings

  set(WARNING_COMPILER_FLAGS "/wd4244 /wd4267")

  set(PROJECT_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNING_COMPILER_FLAGS}")
  set(TEST_CXX_FLAGS "${TEST_CXX_FLAGS} ${WARNING_COMPILER_FLAGS}")
  set(EXAMPLE_C_FLAGS "${CMAKE_C_FLAGS} ${WARNING_COMPILER_FLAGS}")

  # needed for windows sockets (not verified)
  add_definitions(-D_WIN32_WINNT=0x0501)

  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
  add_definitions(-DBOOST_ALL_NO_LIB)
  add_definitions(-DNOMINMAX)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  # CMAKE_CXX_COMPILER variables do not exist in 2.6.4 (min version)
  # Parse the -dumpversion argument into the variable not already set
  if("${CMAKE_CXX_COMPILER_VERSION}" STREQUAL "")
    execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE CMAKE_CXX_COMPILER_VERSION)
  endif()

  # GCC specific compiler options
  # I disabled long-long warning because boost generates about 50 such warnings
  set(WARNING_COMPILER_FLAGS "-Wall -pedantic -Wextra -Wno-long-long -Wno-deprecated-declarations -Wno-unused-parameter")

  if(CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "4.8" OR
     CMAKE_CXX_COMPILER_VERSION VERSION_GREATER "4.8")
    set(WARNING_COMPILER_FLAGS "${WARNING_COMPILER_FLAGS} -Wno-unused-local-typedefs")
  endif()

  set(PROJECT_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC ${WARNING_COMPILER_FLAGS} -Werror")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "4.3" OR
     CMAKE_CXX_COMPILER_VERSION VERSION_GREATER "4.3")
    set(TEST_CXX_FLAGS "${TEST_CXX_FLAGS} ${WARNING_COMPILER_FLAGS}")
  else()
    # TODO(mpenick): stress.cpp might not compile on older compilers because of boost::unique_future<>
    set(TEST_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WARNING_COMPILER_FLAGS}")
  endif()
  set(EXAMPLE_C_FLAGS "${CMAKE_C_FLAGS} ${WARNING_COMPILER_FLAGS}")
else()
  # Clang/Intel specific compiler options
  # I disabled long-long warning because boost generates about 50 such warnings
  set(WARNING_COMPILER_FLAGS "-Wall -pedantic -Wextra -Wno-long-long -Wno-deprecated-declarations -Wno-unused-parameter")

  set(PROJECT_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC ${WARNING_COMPILER_FLAGS} -Werror")
  set(TEST_CXX_FLAGS "${TEST_CXX_FLAGS} ${WARNING_COMPILER_FLAGS}")
  set(EXAMPLE_C_FLAGS "${CMAKE_C_FLAGS} -std=c89 ${WARNING_COMPILER_FLAGS}")
endif()

#-------------------
# Cassandra static and shared
#-------------------

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# we must add header files as dependencies (if header
# changes project must be recompiled, right).
file(GLOB INC_FILES ${PROJECT_SOURCE_DIR}/include/*.h)
file(GLOB SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cpp) 

if(CASS_USE_OPENSSL)
  set(SRC_FILES ${SRC_FILES}
    ${PROJECT_SOURCE_DIR}/src/ssl/ssl_openssl_impl.cpp
    ${PROJECT_SOURCE_DIR}/src/ssl/ring_buffer_bio.cpp)
  add_definitions(-DCASS_USE_OPENSSL)
else()
  set(SRC_FILES ${SRC_FILES}
    ${PROJECT_SOURCE_DIR}/src/ssl/ssl_no_impl.cpp)
endif()

if(Boost_FOUND)
  message(WARNING "Using the external version of Boost")

  if(NOT (Boost_ATOMIC_FOUND AND Boost_RANDOM_FOUND AND Boost_SYSTEM_FOUND))
    message(FATAL_ERROR "Boost atomic, random, and system required to build driver")
  endif()

  set(INCLUDES ${INCLUDES} ${Boost_INCLUDE_DIRS})
  set(LIBS ${LIBS} ${Boost_LIBRARIES})

  set(Boost_USE_STATIC_LIBS OFF)
  set(Boost_USE_STATIC_RUNTIME OFF)
  set(Boost_USE_MULTITHREADED ON)

  add_definitions(-DBOOST_ALL_DYN_LINK)
else()
  set(INCLUDES ${INCLUDES} ${PROJECT_SOURCE_DIR}/src/third_party/boost)
  set(SRC_FILES ${SRC_FILES}
    ${PROJECT_SOURCE_DIR}/src/third_party/boost/libs/atomic/src/lockpool.cpp
    ${PROJECT_SOURCE_DIR}/src/third_party/boost/libs/random/src/random_device.cpp)
endif()

source_group("Source Files" FILES ${SRC_FILES})
source_group("Header Files" FILES ${INC_FILES})

set(ALL_SOURCE_FILES ${SRC_FILES} ${INC_FILES})

# build dynamic and static version of library
add_library(${PROJECT_LIB_NAME} SHARED ${ALL_SOURCE_FILES})
target_link_libraries(${PROJECT_LIB_NAME} ${LIBS})
set_target_properties(${PROJECT_LIB_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_LIB_NAME})
set_target_properties(${PROJECT_LIB_NAME} PROPERTIES VERSION ${PROJECT_VERSION_STRING} SOVERSION ${PROJECT_VERSION_MAJOR})
set_property(
  TARGET ${PROJECT_LIB_NAME}
  APPEND PROPERTY COMPILE_FLAGS "${PROJECT_CXX_FLAGS} -DCASS_BUILDING")

if(CASS_BUILD_STATIC)
  add_library(${PROJECT_LIB_NAME_STATIC} STATIC ${ALL_SOURCE_FILES})
  target_link_libraries(${PROJECT_LIB_NAME_STATIC} ${LIBS})
  set_target_properties(${PROJECT_LIB_NAME_STATIC} PROPERTIES OUTPUT_NAME ${PROJECT_LIB_NAME_STATIC})
  set_target_properties(${PROJECT_LIB_NAME_STATIC} PROPERTIES VERSION ${PROJECT_VERSION_STRING} SOVERSION ${PROJECT_VERSION_MAJOR})
  set_property(
    TARGET ${PROJECT_LIB_NAME_STATIC}
    APPEND PROPERTY COMPILE_FLAGS "${PROJECT_CXX_FLAGS} -DCASS_STATIC")
endif()

include_directories(${INCLUDES})

#-------------------
# Install target
#-------------------

if(CASS_INSTALL_HEADER)
  # Where to put headers
  set(INSTALL_HEADERS_DIR "include")
  install(FILES ${INC_FILES} DESTINATION "${INSTALL_HEADERS_DIR}")
endif()

# Where to put libraries - either lib or lib64.
get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)

if("${LIB64}" STREQUAL "TRUE")
    set(LIBSUFFIX 64)
else()
    set(LIBSUFFIX "")
endif()

set(INSTALL_LIB_DIR "lib${LIBSUFFIX}")

# For windows only
set(INSTALL_DLL_DIR "bin")

install(TARGETS ${PROJECT_LIB_NAME}
  RUNTIME DESTINATION ${INSTALL_DLL_DIR}  # for dll files
  LIBRARY DESTINATION ${INSTALL_LIB_DIR}  # for shared library
  ARCHIVE DESTINATION ${INSTALL_LIB_DIR}) # for static library

if(CASS_BUILD_STATIC)
  install(TARGETS ${PROJECT_LIB_NAME_STATIC}
    RUNTIME DESTINATION ${INSTALL_DLL_DIR}  # for dll files
    LIBRARY DESTINATION ${INSTALL_LIB_DIR}  # for shared library
    ARCHIVE DESTINATION ${INSTALL_LIB_DIR}) # for static library
endif()

#-------------------
# Uninstall target
#-------------------

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)

add_custom_target(UNINSTALL
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

#-------------------
# Integration and unit tests
#-------------------

if(CASS_BUILD_TESTS)
  # Create a testing option for directives that change intended driver functionality
  option(CASS_USE_TESTING_DIRECTIVE "Use testing directive" OFF)
  if(CASS_USE_TESTING_DIRECTIVE)
    message(warning, "Disable testing directive before using the driver in a production environment")
    add_definitions(-DTESTING_DIRECTIVE)
  endif()

  if(WIN32)
    set(LIBSSH2_ROOT "${PROJECT_SOURCE_DIR}/lib/libssh2/")
  endif()

  set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/test/ccm_bridge/cmake/Modules/")
  find_package(LIBSSH2)

  if(Boost_UNIT_TEST_FRAMEWORK_FOUND)
    add_subdirectory(test/unit_tests)
  else()
    message(WARNING "Not building unit tests because of missing dependencies (Boost unit_test_framework)")
  endif()

  if(Boost_CHRONO_FOUND AND Boost_DATE_TIME_FOUND AND
     Boost_SYSTEM_FOUND AND Boost_THREAD_FOUND AND
     Boost_UNIT_TEST_FRAMEWORK_FOUND AND LIBSSH2_FOUND)
    add_subdirectory(test/ccm_bridge)
    add_subdirectory(test/integration_tests)
  else()
    message(WARNING "Not building integration tests because of missing dependencies (libss2 and Boost chrono, date_time, log, log_setup, system, thread, unit_test_framework)")
  endif()
endif()

#-------------------
# Examples
#-------------------

if(CASS_BUILD_EXAMPLES)
  add_subdirectory(examples/async)
  add_subdirectory(examples/basic)
  add_subdirectory(examples/batch)
  add_subdirectory(examples/bind_by_name)
  add_subdirectory(examples/callbacks)
  add_subdirectory(examples/collections)
  add_subdirectory(examples/maps)
  add_subdirectory(examples/prepared)
  add_subdirectory(examples/uuids)
  add_subdirectory(examples/schema_meta)
  add_subdirectory(examples/simple)
  add_subdirectory(examples/ssl)
  add_subdirectory(examples/paging)
  add_subdirectory(examples/perf)
endif()

#-----------------------------------
# Generating API docs with Doxygen
#-----------------------------------

find_package(Doxygen)

if(DOXYGEN_FOUND)
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target(doc
    ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen" VERBATIM)
endif(DOXYGEN_FOUND)

#-----------------------------------
# Build an RPM.
#-----------------------------------
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION_STRING})
set(CPACK_GENERATOR "RPM")
set(CPACK_PACKAGE_NAME "cpp-cassandra-driver")
set(CPACK_PACKAGE_RELEASE 1)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++ driver for Cassandra")
set(CPACK_PACKAGE_CONTACT "Michael Penick")
set(CPACK_PACKAGE_VENDOR "Datastax")
if(32BIT)
  set(CPACK_RPM_PACKAGE_ARCHITECTURE i686)
else()
  set(CPACK_RPM_PACKAGE_ARCHITECTURE x86_64)
endif()
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}.${CPACK_RPM_PACKAGE_ARCHITECTURE}")
set(CPACK_RPM_PACKAGE_REQUIRES "libuv")
include(CPack)
