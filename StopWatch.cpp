
#include "StopWatch.h"

namespace sw
{
	namespace detail
	{
		static constexpr auto clockId = CLOCK_MONOTONIC_RAW;
	}

	StopWatch::StopWatch(const bool start) noexcept
	{
		if (start)
			Start();
	}

	void StopWatch::Start() noexcept
	{
		[[maybe_unused]] const auto err = clock_gettime(detail::clockId, &_cache);
		assert(err == 0);

		_start = 1e9 * static_cast<double>(_cache.tv_sec) + static_cast<double>(_cache.tv_nsec);
	}

	void StopWatch::Stop() noexcept
	{
		[[maybe_unused]] const auto err = clock_gettime(detail::clockId, &_cache);
		assert(err == 0);

		_end = 1e9 * static_cast<double>(_cache.tv_sec) + static_cast<double>(_cache.tv_nsec);
	}

	void StopWatch::Reset() noexcept
	{
		_start = _end = 0.0;
		Start();
	}

}	 // namespace sw
