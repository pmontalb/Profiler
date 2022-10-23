include_guard()

cmake_minimum_required(VERSION 3.14)
project(Profiler)

set(FLAGS_CPP_STANDARD 17)  # this is needed for testing parallel stl
include(${CMAKE_SOURCE_DIR}/cmake/All.cmake)

option(USE_VALGRIND ON)

add_subdirectory(json ${CMAKE_BINARY_DIR}/Profiler/json EXCLUDE_FROM_ALL)

set(VALGRIND_HEADERS "")
mark_as_advanced(VALGRIND_HEADERS)
set(VALGRIND_DEFINE "")
mark_as_advanced(VALGRIND_DEFINE)
if (USE_VALGRIND)
	if (NOT VALGRIND_INCLUDE_DIR)
		message(FATAL_ERROR "valgrind not found")
		return()
	endif()
	message(STATUS "compiling with valgrind support using ${VALGRIND_PROGRAM} and including ${VALGRIND_INCLUDE_DIR}")
	set(VALGRIND_HEADERS ${VALGRIND_INCLUDE_DIR})
	set(VALGRIND_DEFINE HAS_VALGRIND)
endif()


create_library(
	NAME
		StopWatch
	SOURCES
		StopWatch.cpp
)

create_library(
	NAME
		Profiler
	SOURCES
		Profiler.cpp
	DEPENDENCIES
		StopWatch
	PUBLIC_INCLUDE_DIRECTORIES
		${VALGRIND_HEADERS} . json/single_include
	PUBLIC_COMPILE_DEFINITIONS
		${VALGRIND_DEFINE}
)

create_executable(
	NAME
		example
	SOURCES
		example.cpp
	DEPENDENCIES
		Profiler
	PRIVATE_INCLUDE_DIRECTORIES
		.
)

create_executable(
	NAME
		exampleStl
	SOURCES
		exampleStl.cpp
	DEPENDENCIES
		Profiler
	SYSTEM_DEPENDENCIES
		tbb  # this needs TBB >= 2018, so on Ubuntu <= 18.04 you need to install the latest TBB
	PRIVATE_INCLUDE_DIRECTORIES
		.
)
