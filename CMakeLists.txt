include_guard()

cmake_minimum_required(VERSION 3.28)
project(Profiler LANGUAGES CXX)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/All.cmake)

option(USE_VALGRIND "Enable Valgrind Callgrind instrumentation support" OFF)
option(USE_VTUNE "Enable Intel VTune ITT instrumentation support" OFF)

set(_profiler_sys_includes "${CMAKE_CURRENT_SOURCE_DIR}/json/single_include")
set(_profiler_compile_defs "")
set(_profiler_sys_deps "")

if (USE_VALGRIND)
	find_path(VALGRIND_INCLUDE_DIR NAMES valgrind/callgrind.h)
	if (NOT VALGRIND_INCLUDE_DIR)
		message(FATAL_ERROR "USE_VALGRIND is ON, but valgrind/callgrind.h was not found")
	endif()
	message(STATUS "Compiling with Valgrind Callgrind support using ${VALGRIND_INCLUDE_DIR}")
	list(APPEND _profiler_sys_includes "${VALGRIND_INCLUDE_DIR}")
	list(APPEND _profiler_compile_defs "HAS_VALGRIND")
endif()

if (USE_VTUNE)
	find_package(Threads REQUIRED)
	find_path(ITTNOTIFY_INCLUDE_DIR
		NAMES ittnotify.h
		PATHS
			ENV VTUNE_PROFILER_DIR
			ENV INTEL_ONEAPI_ROOT
			/opt/intel/oneapi/vtune/latest
		PATH_SUFFIXES include
	)
	find_library(ITTNOTIFY_LIBRARY
		NAMES ittnotify
		PATHS
			ENV VTUNE_PROFILER_DIR
			ENV INTEL_ONEAPI_ROOT
			/opt/intel/oneapi/vtune/latest
		PATH_SUFFIXES lib64 lib
	)
	if (NOT ITTNOTIFY_INCLUDE_DIR OR NOT ITTNOTIFY_LIBRARY)
		message(FATAL_ERROR "USE_VTUNE is ON, but ittnotify.h or libittnotify was not found")
	endif()
	message(STATUS "Compiling with Intel VTune ITT support using ${ITTNOTIFY_INCLUDE_DIR} and ${ITTNOTIFY_LIBRARY}")
	list(APPEND _profiler_sys_includes "${ITTNOTIFY_INCLUDE_DIR}")
	list(APPEND _profiler_compile_defs "HAS_VTUNE")
	list(APPEND _profiler_sys_deps "${ITTNOTIFY_LIBRARY}" "${CMAKE_DL_LIBS}" Threads::Threads)
endif()

create_library(
	NAME
		StopWatch
	SOURCES
		StopWatch.cpp
	PUBLIC_INCLUDE_DIRECTORIES
		"${CMAKE_CURRENT_SOURCE_DIR}"
)

create_library(
	NAME
		Profiler
	SOURCES
		Profiler.cpp
	DEPENDENCIES
		StopWatch
	SYSTEM_DEPENDENCIES
		${_profiler_sys_deps}
	PUBLIC_INCLUDE_DIRECTORIES
		"${CMAKE_CURRENT_SOURCE_DIR}"
	PUBLIC_SYSTEM_INCLUDE_DIRECTORIES
		${_profiler_sys_includes}
	PUBLIC_COMPILE_DEFINITIONS
		${_profiler_compile_defs}
)

if (PROJECT_IS_TOP_LEVEL)
	create_executable(
		NAME
			example
		SOURCES
			example.cpp
		DEPENDENCIES
			Profiler
	)

	find_package(TBB QUIET)
	if (TBB_FOUND)
		create_executable(
			NAME
				exampleStl
			SOURCES
				exampleStl.cpp
			DEPENDENCIES
				Profiler
			SYSTEM_DEPENDENCIES
				TBB::tbb
		)
	else()
		message(STATUS "TBB not found; skipping exampleStl target")
	endif()

	if (CMAKE_TESTING_ENABLED)
		add_subdirectory(test)
	endif()
endif()
