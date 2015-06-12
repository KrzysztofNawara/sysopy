cmake_minimum_required(VERSION 3.1)
project(Makefile)

set (SRC ${PROJECT_SOURCE_DIR}/src)
set (TSRC ${PROJECT_SOURCE_DIR}/testsrc)
set (LIBSRC ${PROJECT_SOURCE_DIR}/3rdp)

#other possibility - just GLOB
FILE (GLOB project_source ${SRC}/*.c ${SRC}/*.h ${SRC}/*.ctpl ${SRC}/*.htpl)
FILE (GLOB metafiles Makefile)
FILE (GLOB test_source ${TSRC}/*.c ${TSRC}/*.h)
FILE (GLOB_RECURSE lib_src ${LIBSRC}/*.h)

SET(SOURCE_FILES ${project_source} ${metafiles} ${test_source} ${lib_src})

add_executable(dummy ${SOURCE_FILES})

#INCLUDE_DIRECTORIES(...)

add_custom_target(client.x+debug make -C ${PROJECT_SOURCE_DIR} -f Makefile_CLion client.x debug=1)
add_custom_target(server.x+debug make -C ${PROJECT_SOURCE_DIR} -f Makefile_CLion server.x debug=1)