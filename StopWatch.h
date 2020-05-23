#pragma once

#include <chrono>
#include <cassert>

namespace perf
{
	class Stopwatch
	{
	public:
		explicit Stopwatch(bool startTimer = true) noexcept
		{
			if (startTimer)
				Start();
		}

		inline void Start() noexcept { _start = std::chrono::high_resolution_clock::now(); }
		inline void Stop() noexcept { _stop = std::chrono::high_resolution_clock::now(); }

		inline auto GetNanoSeconds() const noexcept
		{
			assert(_stop >= _start);
			return std::chrono::duration_cast<std::chrono::nanoseconds>(_stop - _start).count();
		}
		inline auto GetMicroSeconds() const noexcept { return static_cast<double>(GetNanoSeconds()) * 1e-3; }
		inline auto GetMilliSeconds() const noexcept { return static_cast<double>(GetNanoSeconds()) * 1e-6; }
		inline auto GetSeconds() const noexcept { return static_cast<double>(GetNanoSeconds()) * 1e-9; }

	private:
		std::chrono::high_resolution_clock::time_point _start {};
		std::chrono::high_resolution_clock::time_point _stop {};
	};
}
