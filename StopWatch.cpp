#include "StopWatch.h"

#ifdef __x86_64__
	#include <chrono>
	#include <cpuid.h>
	#include <thread>
#endif

namespace sw
{
#ifdef __x86_64__
	namespace detail
	{
		static double CalibrateNsPerTick() noexcept
		{
			unsigned int eax = 0;
			unsigned int ebx = 0;
			unsigned int ecx = 0;
			unsigned int edx = 0;

			if (__get_cpuid(0x15, &eax, &ebx, &ecx, &edx) != 0 && eax != 0 && ebx != 0 && ecx != 0)
			{
				const auto tscHz = (static_cast<double>(ecx) * static_cast<double>(ebx)) / static_cast<double>(eax);
				return 1e9 / tscHz;
			}

			// Fallback: calibrate invariant TSC against CLOCK_MONOTONIC_RAW
			StopWatch wallWatch(true);
			TscStopWatch tscWatch(true);
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			tscWatch.Stop();
			wallWatch.Stop();

			const auto ticks = tscWatch.GetTicks();
			const auto ns = wallWatch.GetNanoSeconds();
			assert(ticks > 0 && ns > 0.0);
			return ns / static_cast<double>(ticks);
		}
	}	 // namespace detail

	double TscStopWatch::GetNsPerTick() noexcept
	{
		static const auto nsPerTick = detail::CalibrateNsPerTick();
		return nsPerTick;
	}
#endif

}	 // namespace sw
