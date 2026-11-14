#pragma once

#include "StopWatch.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define PROFILER_LIFETIMEBOUND SW_LIFETIMEBOUND

namespace perf
{
	template<typename T>
	inline void DoNotOptimize(const T& value) noexcept
	{
		__asm__ __volatile__("" : : "g"(&value) : "memory");
	}

	template<typename T>
	inline void DoNotOptimize(T& value) noexcept
	{
		__asm__ __volatile__("" : : "g"(&value) : "memory");
	}

	inline void ClobberMemory() noexcept
	{
		__asm__ __volatile__("" : : : "memory");
	}

	class PerfControl
	{
	public:
		explicit PerfControl(const char* ctlFifoPath = nullptr, const char* ackFifoPath = nullptr) noexcept;
		~PerfControl() noexcept;

		PerfControl(const PerfControl&) = delete;
		PerfControl& operator=(const PerfControl&) = delete;

		[[nodiscard]] inline bool IsActive() const noexcept { return _ctlFd >= 0; }
		bool Enable() noexcept;
		bool Disable() noexcept;

	private:
		bool SendCommand(const char* cmd, std::size_t length) noexcept;
		bool WaitAck() noexcept;

		int _ctlFd = -1;
		int _ackFd = -1;
		bool _ownsCtlFd = false;
		bool _ownsAckFd = false;
		bool _enabled = false;
	};

	class PerfScope
	{
	public:
		explicit PerfScope(const char* ctlFifoPath = nullptr, const char* ackFifoPath = nullptr) noexcept : _control(ctlFifoPath, ackFifoPath)
		{
			_control.Enable();
		}
		~PerfScope() noexcept { _control.Disable(); }

		PerfScope(const PerfScope&) = delete;
		PerfScope& operator=(const PerfScope&) = delete;

		[[nodiscard]] inline bool IsActive() const noexcept { return _control.IsActive(); }

	private:
		PerfControl _control;
	};

	class CallgrindScope
	{
	public:
		CallgrindScope() noexcept;
		~CallgrindScope() noexcept;

		CallgrindScope(const CallgrindScope&) = delete;
		CallgrindScope& operator=(const CallgrindScope&) = delete;
	};

	class VtuneScope
	{
	public:
		explicit VtuneScope(const char* taskName = "Region", const char* domainName = "Profiler") noexcept;
		~VtuneScope() noexcept;

		VtuneScope(const VtuneScope&) = delete;
		VtuneScope& operator=(const VtuneScope&) = delete;

	private:
#ifdef HAS_VTUNE
		void* _domain = nullptr;
		void* _handle = nullptr;
#endif
	};

	enum class TimeScale
	{
		Nanoseconds,
		Microseconds,
		Milliseconds,
		Seconds,
		COUNT,
	};

	[[nodiscard]] static inline std::string ToString(const TimeScale timeScale) noexcept
	{
		switch (timeScale)
		{
			case TimeScale::Nanoseconds:
				return "ns";
			case TimeScale::Microseconds:
				return "us";
			case TimeScale::Milliseconds:
				return "ms";
			case TimeScale::Seconds:
				return "s";
			default:
				return "?";
		}
	}

	namespace detail
	{
		static constexpr std::size_t nPercentiles = 13;
		using Percentiles = std::array<double, nPercentiles>;
		static constexpr Percentiles percentiles = { {
			1.0,
			5.0,
			10.0,
			20.0,
			30.0,
			40.0,
			50.0,
			60.0,
			70.0,
			80.0,
			90.0,
			95.0,
			99.0,
		} };
		static constexpr std::size_t medianIndex = { 6 };

		[[nodiscard]] static inline std::string EscapePythonSingleQuotes(const std::string& input) noexcept
		{
			std::string escaped {};
			escaped.reserve(input.size());
			for (const auto ch : input)
			{
				if (ch == '\\' || ch == '\'')
					escaped += '\\';
				escaped += ch;
			}
			return escaped;
		}

		class Performance
		{
		public:
			explicit Performance(const std::size_t nObservations) noexcept { Reset(nObservations); }

			void Reset(std::size_t nObservations) noexcept;

			inline void Register(const double observation) noexcept { _observations.emplace_back(observation); }

			void ComputeAnalytics() noexcept;

			[[nodiscard]] inline auto GetAverage() const noexcept { return _average * _timeScaleMultiplier; }
			[[nodiscard]] inline auto GetStandardDeviation() const noexcept { return _standardDeviation * _timeScaleMultiplier; }
			[[nodiscard]] inline auto GetMedian() const noexcept { return _percentiles[medianIndex] * _timeScaleMultiplier; }
			[[nodiscard]] inline Percentiles GetPercentiles() const noexcept
			{
				Percentiles scaled {};
				for (std::size_t i = 0; i < nPercentiles; ++i)
					scaled[i] = _percentiles[i] * _timeScaleMultiplier;
				return scaled;
			}
			[[nodiscard]] inline const auto& GetRawPercentiles() const noexcept PROFILER_LIFETIMEBOUND { return _percentiles; }
			[[nodiscard]] inline const auto& GetRawObservations() const noexcept PROFILER_LIFETIMEBOUND { return _observations; }
			[[nodiscard]] inline auto GetTimeScale() const noexcept { return _scale; }
			[[nodiscard]] inline auto GetTimeScaleMultiplier() const noexcept { return _timeScaleMultiplier; }

			[[nodiscard]] nlohmann::json ToJson() const noexcept;
			[[nodiscard]] std::string ToString(const bool printPercentiles = false, const bool printRawObservations = false) const noexcept;
			inline void Print(const bool printPercentiles = false, const bool printRawObservations = false) const noexcept
			{
				std::cout << ToString(printPercentiles, printRawObservations) << std::endl;
			}

		private:
			void ComputeMeanAndStdDev() noexcept;
			void ComputePercentiles() noexcept;
			void ComputeMostFrequentTimeScale() noexcept;

		private:
			std::vector<double> _observations {};
			Percentiles _percentiles {};

			double _average = 0.0;
			double _standardDeviation = 0.0;
			TimeScale _scale = TimeScale::Nanoseconds;
			double _timeScaleMultiplier = 1.0;
		};
	}	 // namespace detail

	struct Config
	{
		std::size_t nIterations = 100;
		std::size_t nIterationsPerCycle = 1;
		std::size_t nWarmUpIterations = 1;

		[[nodiscard]] inline std::string ToString() const noexcept
		{
			std::string ret {};
			ret.reserve(128);

			ret += "#Iterations           : \t" + std::to_string(nIterations) + "\n";
			ret += "#Iterations Per Cycle : \t" + std::to_string(nIterationsPerCycle) + "\n";
			ret += "#WarmUp Iterations    : \t" + std::to_string(nWarmUpIterations) + "\n";

			return ret;
		}
		inline void Print() const noexcept { std::cout << ToString() << std::endl; }
	};

	template<typename ProfilerImpl, typename ConfigImpl, typename StopWatchT = sw::StopWatch>
	class Profiler
	{
	public:
		explicit Profiler(const ConfigImpl& config) noexcept : _config(config) {}

		inline void Profile() noexcept
		{
			OnStart();

			assert(_config.nIterationsPerCycle > 0);
			if (_config.nIterationsPerCycle == 0)
			{
				OnEnd();
				return;
			}

			RunWarmUp(_config.nWarmUpIterations);

			StopWatchT stopWatch(false);
			for (std::size_t i = 0; i < _config.nIterations; ++i)
			{
				static_cast<ProfilerImpl*>(this)->PreRunImpl();

				stopWatch.Start();
				for (std::size_t j = 0; j < _config.nIterationsPerCycle; ++j)
					static_cast<ProfilerImpl*>(this)->RunImpl();
				stopWatch.Stop();

				static_cast<ProfilerImpl*>(this)->PostRunImpl();

				const auto elapsedTime = stopWatch.GetNanoSeconds() / static_cast<double>(_config.nIterationsPerCycle);
				_performance.Register(elapsedTime);
			}

			OnEnd();
		}

		/*
		 * Run with `valgrind --tool=callgrind --instr-atstart=no ./binary`
		 * This will produce an output such as callgrind.out.PID.1
		 * Analyse the output with kcachegrind
		 */
#ifndef HAS_VALGRIND
		inline void InstrumentCallgrind(std::size_t = 0, std::size_t = 1) noexcept
		{
			std::abort();
		}
#else
		inline void InstrumentCallgrind(const std::size_t nWarmUpIterations = 0, const std::size_t nIterations = 1) noexcept
		{
			RunWarmUp(nWarmUpIterations);
			const CallgrindScope scope {};
			RunInstrumentedIterations(nIterations);
		}
#endif

		inline void Instrument(const std::size_t nWarmUpIterations = 0, const std::size_t nIterations = 1) noexcept
		{
			InstrumentCallgrind(nWarmUpIterations, nIterations);
		}

		/*
		 * Run with e.g.:
		 *   mkfifo perf_ctl.fifo perf_ack.fifo
		 *   PERF_CTL_FIFO=perf_ctl.fifo PERF_ACK_FIFO=perf_ack.fifo \
		 *       perf record --delay=-1 --control=fifo:perf_ctl.fifo,perf_ack.fifo ./binary
		 */
		inline bool InstrumentPerf(const std::size_t nWarmUpIterations = 0, const std::size_t nIterations = 1, const char* ctlFifoPath = nullptr, const char* ackFifoPath = nullptr) noexcept
		{
			RunWarmUp(nWarmUpIterations);
			const PerfScope scope(ctlFifoPath, ackFifoPath);
			RunInstrumentedIterations(nIterations);
			return scope.IsActive();
		}

		/*
		 * Build with `-DUSE_VTUNE=ON` and run with e.g.:
		 *   vtune -collect hotspots -start-paused ./binary
		 */
#ifndef HAS_VTUNE
		inline void InstrumentVTune(std::size_t = 0, std::size_t = 1, const char* = "RunImpl", const char* = "Profiler") noexcept
		{
			std::abort();
		}
#else
		inline void InstrumentVTune(const std::size_t nWarmUpIterations = 0, const std::size_t nIterations = 1, const char* taskName = "RunImpl", const char* domainName = "Profiler") noexcept
		{
			RunWarmUp(nWarmUpIterations);
			const VtuneScope scope(taskName, domainName);
			RunInstrumentedIterations(nIterations);
		}
#endif

		inline void OnStart() noexcept
		{
			_performance.Reset(_config.nIterations);
			static_cast<ProfilerImpl*>(this)->OnStartImpl();
		}

		inline void OnEnd() noexcept
		{
			_performance.ComputeAnalytics();
			static_cast<ProfilerImpl*>(this)->OnEndImpl();
		}

		[[nodiscard]] inline const auto& GetPerformance() const noexcept PROFILER_LIFETIMEBOUND { return _performance; }
		[[nodiscard]] inline double GetAverage() const noexcept { return GetPerformance().GetAverage(); }
		[[nodiscard]] inline double GetStandardDeviation() const noexcept { return GetPerformance().GetStandardDeviation(); }
		[[nodiscard]] inline double GetMedian() const noexcept { return GetPerformance().GetMedian(); }
		[[nodiscard]] inline detail::Percentiles GetPercentiles() const noexcept { return GetPerformance().GetPercentiles(); }
		[[nodiscard]] inline const auto& GetRawPercentiles() const noexcept PROFILER_LIFETIMEBOUND { return GetPerformance().GetRawPercentiles(); }
		[[nodiscard]] inline const auto& GetRawObservations() const noexcept PROFILER_LIFETIMEBOUND { return GetPerformance().GetRawObservations(); }
		[[nodiscard]] inline TimeScale GetTimeScale() const noexcept { return GetPerformance().GetTimeScale(); }

		[[nodiscard]] inline nlohmann::json ToJson() const noexcept
		{
			return _performance.ToJson();
		}

		[[nodiscard]] inline std::string ToString(const bool printPercentiles = false, const bool printRawObservations = false) const noexcept
		{
			auto ret = _config.ToString();
			ret += _performance.ToString(printPercentiles, printRawObservations);
			return ret;
		}
		inline void Print(const bool printPercentiles = false, const bool printRawObservations = false) const noexcept
		{
			std::cout << ToString(printPercentiles, printRawObservations) << std::endl;
		}

		[[nodiscard]] inline std::string PercentilesToCsv(const bool includePercentileKeys = false) const noexcept
		{
			const auto scaledPercentiles = _performance.GetPercentiles();
			std::string ret {};
			ret.reserve(256);

			if (includePercentileKeys)
			{
				for (std::size_t i = 0; i < detail::nPercentiles; ++i)
				{
					if (i > 0)
						ret += ",";
					ret += std::to_string(detail::percentiles[i]);
				}
				ret += "\n";
			}

			for (std::size_t i = 0; i < detail::nPercentiles; ++i)
			{
				if (i > 0)
					ret += ",";
				ret += std::to_string(scaledPercentiles[i]);
			}
			ret += "\n";

			return ret;
		}
		inline void PrintPercentilesCsv(const bool includePercentileKeys = false) const noexcept
		{
			std::cout << PercentilesToCsv(includePercentileKeys) << std::endl;
		}

		[[nodiscard]] inline std::string AverageToString() const noexcept
		{
			const auto mid = _performance.GetAverage();
			const auto stdev = _performance.GetStandardDeviation();
			const auto lo = mid - stdev;
			const auto hi = mid + stdev;
			const auto unit = perf::ToString(_performance.GetTimeScale());
			return std::string("Average: ") + std::to_string(mid) + " " + unit + " [ " + std::to_string(lo) + ", " + std::to_string(hi) + " ]";
		}
		inline void PrintAverage() const noexcept { std::cout << AverageToString() << std::endl; }

		[[nodiscard]] inline std::string ToPythonPlot(const bool show, const std::string& label = "", const bool includeRawObservations = false) const noexcept
		{
			const auto scaledPercentiles = _performance.GetPercentiles();
			const auto& rawObservations = _performance.GetRawObservations();
			const auto scaleMultiplier = _performance.GetTimeScaleMultiplier();
			const auto unit = perf::ToString(_performance.GetTimeScale());
			const auto escapedLabel = detail::EscapePythonSingleQuotes(label);

			std::string ret {};
			ret.reserve(512 + (includeRawObservations ? rawObservations.size() * 16 : 0));
			ret += "import matplotlib.pyplot as plt;";
			ret += "import numpy as np;";

			ret += "percentiles=[";
			for (std::size_t i = 0; i < detail::nPercentiles; ++i)
			{
				if (i > 0)
					ret += ",";
				ret += std::to_string(detail::percentiles[i]);
			}
			ret += "];";

			ret += "percentileValues=[";
			for (std::size_t i = 0; i < detail::nPercentiles; ++i)
			{
				if (i > 0)
					ret += ",";
				ret += std::to_string(scaledPercentiles[i]);
			}
			ret += "];";

			if (includeRawObservations)
			{
				ret += "observations=[";
				for (std::size_t i = 0; i < rawObservations.size(); ++i)
				{
					if (i > 0)
						ret += ",";
					ret += std::to_string(rawObservations[i] * scaleMultiplier);
				}
				ret += "];";

				ret += "fig = plt.figure(1);";
				ret += "ax = fig.add_subplot(121);";
				ret += "n,bins,patches=ax.hist(x=observations,bins='auto',color='#0504aa',alpha=0.7);";
				ret += "ax.grid(alpha=0.75);";
				ret += "ax.set_xlabel('Time (" + unit + ")');";
				ret += "max_freq=n.max() if len(n) > 0 else 0;";
				ret += "ax.set_ylim(ymax=np.ceil(max_freq/10)*10 if max_freq%10 else max_freq+10);";

				ret += "ax = fig.add_subplot(122);";
				ret += "ax.plot(percentiles, percentileValues, label='" + escapedLabel + "');";
				ret += "ax.set_xlabel('Percentile (%)');";
				ret += "ax.set_ylabel('Time (" + unit + ")');";
				ret += "ax.grid(alpha=0.75);";
				ret += "ax.legend(loc='best');";
			}
			else
			{
				ret += "fig = plt.figure(1);";
				ret += "ax = fig.add_subplot(111);";
				ret += "ax.plot(percentiles, percentileValues, label='" + escapedLabel + "');";
				ret += "ax.set_xlabel('Percentile (%)');";
				ret += "ax.set_ylabel('Time (" + unit + ")');";
				ret += "ax.grid(alpha=0.75);";
				ret += "ax.legend(loc='lower right');";
			}

			if (show)
				ret += "plt.show();";

			return ret;
		}
		inline void PrintPythonPlotInstructions(const bool show, const std::string& label = "", const bool includeRawObservations = false) const noexcept
		{
			std::cout << ToPythonPlot(show, label, includeRawObservations) << std::endl;
		}

	protected:
		constexpr void OnStartImpl() noexcept {}
		constexpr void OnEndImpl() noexcept {}
		constexpr void PreRunImpl() noexcept {}
		constexpr void PostRunImpl() noexcept {}

		const ConfigImpl _config;

	private:
		inline void RunWarmUp(const std::size_t nWarmUpIterations) noexcept
		{
			for (std::size_t i = 0; i < nWarmUpIterations; ++i)
				static_cast<ProfilerImpl*>(this)->RunImpl();
		}

		inline void RunInstrumentedIterations(const std::size_t nIterations) noexcept
		{
			for (std::size_t i = 0; i < nIterations; ++i)
			{
				static_cast<ProfilerImpl*>(this)->PreRunImpl();
				static_cast<ProfilerImpl*>(this)->RunImpl();
				static_cast<ProfilerImpl*>(this)->PostRunImpl();
			}
		}

		detail::Performance _performance { _config.nIterations };
	};
}	 // namespace perf
