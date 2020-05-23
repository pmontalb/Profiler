include_guard()

cmake_minimum_required(VERSION 3.14)
project(Profiler)

include(${CMAKE_SOURCE_DIR}/cmake/All.cmake)

option(USE_VALGRIND ON)

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
		Profiler
	SOURCES
		Profiler.h StopWatch.h
	PUBLIC_INCLUDE_DIRECTORIES
		${VALGRIND_HEADERS}
	PUBLIC_COMPILE_DEFINITIONS
		${VALGRIND_DEFINE}
)
set_target_properties(Profiler PROPERTIES LINKER_LANGUAGE CXX)

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