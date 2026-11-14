if (NOT CMAKE_BUILD_TYPE STREQUAL "Msan")
	find_package(GTest REQUIRED)
	find_package(Threads REQUIRED)

	create_test(
		NAME
			ProfilerTests
		SOURCES
			main.cpp
			StopWatchUnitTests.cpp
			ProfilerUnitTests.cpp
		DEPENDENCIES
			Profiler
		SYSTEM_DEPENDENCIES
			GTest::gtest Threads::Threads
	)
else()
	create_test(
		NAME
			ProfilerMsanSmoke
		SOURCES
			MsanSmoke.cpp
		DEPENDENCIES
			Profiler
	)
endif()
