#pragma once

#include <cassert>
#include <chrono>

namespace sw
{
	class StopWatch
	{
	public:
		explicit StopWatch(bool startTimer = true) noexcept;

		void Start() noexcept;
		void Stop() noexcept;
		void Reset() noexcept;

		[[nodiscard]] inline double GetNanoSeconds() const noexcept { return _end - _start; }
		[[nodiscard]] inline auto GetMicroSeconds() const noexcept { return GetNanoSeconds() * 1e-3; }
		[[nodiscard]] inline auto GetMilliSeconds() const noexcept { return GetNanoSeconds() * 1e-6; }
		[[nodiscard]] inline auto GetSeconds() const noexcept { return GetNanoSeconds() * 1e-9; }

		template<typename OStream>
		friend OStream& operator<<(OStream& os, const StopWatch& sw)
		{
			auto ns = sw.GetNanoSeconds();
			assert(ns >= 0.0);

			if (ns < 1e3)
				os << ns << "ns";
			else if (ns < 1e6)
				os << 1e-3 * ns << "us";
			else if (ns < 1e9)
				os << 1e-6 * ns << "ms";
			else
				os << 1e-9 * ns << "s";

			return os;
		}

	private:
		double _start = 0;
		double _end = 0;
		timespec _cache {};
	};
}	 // namespace sw
