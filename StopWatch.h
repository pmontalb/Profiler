#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <ctime>

#ifdef __x86_64__
	#include <x86intrin.h>
#endif

#if defined(__clang__) && defined(__has_cpp_attribute)
	#if __has_cpp_attribute(clang::lifetimebound)
		#define SW_LIFETIMEBOUND [[clang::lifetimebound]]
	#else
		#define SW_LIFETIMEBOUND
	#endif
#else
	#define SW_LIFETIMEBOUND
#endif

namespace sw
{
	namespace detail
	{
		template<typename OStream>
		inline void FormatDuration(OStream& os, const double ns)
		{
			assert(ns >= 0.0);

			if (ns < 1e3)
				os << ns << "ns";
			else if (ns < 1e6)
				os << 1e-3 * ns << "us";
			else if (ns < 1e9)
				os << 1e-6 * ns << "ms";
			else
				os << 1e-9 * ns << "s";
		}

		template<typename Derived>
		class StopWatchBase
		{
		public:
			inline void Reset(const bool startTimer = true) noexcept
			{
				auto* const self = static_cast<Derived*>(this);
				if (startTimer)
					self->Start();
				else
					self->Clear();
			}

			[[nodiscard]] inline double GetMicroSeconds() const noexcept { return GetNano() * 1e-3; }
			[[nodiscard]] inline double GetMilliSeconds() const noexcept { return GetNano() * 1e-6; }
			[[nodiscard]] inline double GetSeconds() const noexcept { return GetNano() * 1e-9; }

			template<typename OStream>
			friend OStream& operator<<(OStream& os SW_LIFETIMEBOUND, const Derived& sw)
			{
				FormatDuration(os, sw.GetNanoSeconds());
				return os;
			}

		private:
			[[nodiscard]] inline double GetNano() const noexcept
			{
				return static_cast<const Derived*>(this)->GetNanoSeconds();
			}
		};
	}	 // namespace detail

	class StopWatch : public detail::StopWatchBase<StopWatch>
	{
		friend class detail::StopWatchBase<StopWatch>;

	public:
		explicit inline StopWatch(const bool startTimer = true) noexcept
		{
			if (startTimer)
				Start();
		}

		inline void Start() noexcept
		{
			[[maybe_unused]] const auto err = clock_gettime(CLOCK_MONOTONIC_RAW, &_startTs);
			assert(err == 0);
			_endTs = _startTs;
			std::atomic_signal_fence(std::memory_order_acq_rel);
		}

		inline void Stop() noexcept
		{
			std::atomic_signal_fence(std::memory_order_acq_rel);
			[[maybe_unused]] const auto err = clock_gettime(CLOCK_MONOTONIC_RAW, &_endTs);
			assert(err == 0);
		}

		[[nodiscard]] inline double GetNanoSeconds() const noexcept
		{
			const auto deltaSec = static_cast<std::int64_t>(_endTs.tv_sec) - static_cast<std::int64_t>(_startTs.tv_sec);
			const auto deltaNsec = static_cast<std::int64_t>(_endTs.tv_nsec) - static_cast<std::int64_t>(_startTs.tv_nsec);
			return static_cast<double>(deltaSec * 1'000'000'000LL + deltaNsec);
		}

	private:
		inline void Clear() noexcept
		{
			_startTs = {};
			_endTs = {};
		}

		std::timespec _startTs {};
		std::timespec _endTs {};
	};

	template<typename ClockT = std::chrono::steady_clock>
	class ChronoStopWatch : public detail::StopWatchBase<ChronoStopWatch<ClockT>>
	{
		friend class detail::StopWatchBase<ChronoStopWatch<ClockT>>;

	public:
		explicit inline ChronoStopWatch(const bool startTimer = true) noexcept
		{
			if (startTimer)
				Start();
		}

		inline void Start() noexcept
		{
			_start = ClockT::now();
			_end = _start;
			std::atomic_signal_fence(std::memory_order_acq_rel);
		}

		inline void Stop() noexcept
		{
			std::atomic_signal_fence(std::memory_order_acq_rel);
			_end = ClockT::now();
		}

		[[nodiscard]] inline double GetNanoSeconds() const noexcept
		{
			return std::chrono::duration<double, std::nano>(_end - _start).count();
		}

	private:
		inline void Clear() noexcept
		{
			_start = {};
			_end = {};
		}

		typename ClockT::time_point _start {};
		typename ClockT::time_point _end {};
	};

#ifdef __x86_64__
	class TscStopWatch : public detail::StopWatchBase<TscStopWatch>
	{
		friend class detail::StopWatchBase<TscStopWatch>;

	public:
		explicit inline TscStopWatch(const bool startTimer = true) noexcept
		{
			if (startTimer)
				Start();
		}

		inline void Start() noexcept
		{
			_mm_lfence();
			_startTsc = __rdtsc();
			_mm_lfence();
			_endTsc = _startTsc;
			std::atomic_signal_fence(std::memory_order_acq_rel);
		}

		inline void Stop() noexcept
		{
			std::atomic_signal_fence(std::memory_order_acq_rel);
			unsigned int aux = 0;
			_endTsc = __rdtscp(&aux);
			_mm_lfence();
		}

		[[nodiscard]] inline std::uint64_t GetTicks() const noexcept { return _endTsc - _startTsc; }
		[[nodiscard]] inline double GetNanoSeconds() const noexcept { return static_cast<double>(GetTicks()) * GetNsPerTick(); }

		[[nodiscard]] static double GetNsPerTick() noexcept;
		[[nodiscard]] static double GetFrequencyHz() noexcept { return 1e9 / GetNsPerTick(); }

	private:
		inline void Clear() noexcept
		{
			_startTsc = 0;
			_endTsc = 0;
		}

		std::uint64_t _startTsc = 0;
		std::uint64_t _endTsc = 0;
	};
#endif
}	 // namespace sw
