
#pragma once

#include "StopWatch.h"

#ifdef HAS_VALGRIND
	#include <valgrind/callgrind.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace perf
{
	enum class TimeScale
	{
		Nanoseconds,
		Microseconds,
		Milliseconds,
		Seconds,
		COUNT,
	};

	static inline std::string ToString(const TimeScale timeScale) noexcept
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
		static constexpr size_t nPercentiles = 13;
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
		static constexpr size_t medianIndex = { 6 };

		class Performance
		{
		public:
			explicit Performance(size_t nObservations) noexcept { Reset(nObservations); }

			void Reset(size_t nObservations) noexcept;

			inline void Register(double observation) noexcept { _observations.emplace_back(observation); }

			void ComputeAnalytics() noexcept;

			inline auto GetAverage() const noexcept { return _average * _timeScaleMultiplier; }
			inline auto GetStandardDeviation() const noexcept { return _standardDeviation * _timeScaleMultiplier; }
			inline auto GetMedian() const noexcept { return _percentiles[medianIndex] * _timeScaleMultiplier; }
			inline const auto& GetPercentiles() const noexcept { return _percentiles; }
			inline const auto& GetRawObservations() const noexcept { return _observations; }

			std::string ToString(const bool printPercentiles = false, const bool printRawObservations = false) const noexcept;
			inline void Print(const bool printRawObservations = false) const noexcept { std::cout << ToString(printRawObservations) << std::endl; }

		private:
			void ComputeAverage() noexcept;
			void ComputeStandardDeviation() noexcept;
			void ComputePercentiles() noexcept;
			void ComputeMostFrequentTimeScale() noexcept;
		private:
			std::vector<double> _observations {};
			Percentiles _percentiles {};

			double _average = 0.0;
			double _standardDeviation = 0.0;
			TimeScale _scale = TimeScale::Nanoseconds;
			double _timeScaleMultiplier = 0.0;
		};
	}	 // namespace detail

	struct Config
	{
		size_t nIterations = 100;
		size_t nIterationsPerCycle = 1;
		size_t nWarmUpIterations = 1;

		inline std::string ToString() const noexcept
		{
			std::string ret {};

			ret += "#Iterations           : \t" + std::to_string(nIterations) + "\n";
			ret += "#Iterations Per Cycle : \t" + std::to_string(nIterationsPerCycle) + "\n";
			ret += "#WarmUp Iterations    : \t" + std::to_string(nWarmUpIterations) + "\n";

			return ret;
		}
		inline void Print() const noexcept { std::cout << ToString() << std::endl; }
	};

	template<typename ProfilerImpl, typename ConfigImpl>
	class Profiler
	{
	public:
		explicit Profiler(const ConfigImpl& config) noexcept : _config(config) {}

		inline void Profile() noexcept
		{
			OnStart();

			// warm up caches
			for (size_t i = 0; i < _config.nWarmUpIterations; ++i)
				static_cast<ProfilerImpl*>(this)->RunImpl();

			sw::StopWatch stopWatch(false);
			for (size_t i = 0; i < _config.nIterations; ++i)
			{
				stopWatch.Start();
				for (size_t j = 0; j < _config.nIterationsPerCycle; ++j)
					static_cast<ProfilerImpl*>(this)->RunImpl();
				stopWatch.Stop();

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
		inline void Instrument() noexcept
		{
#ifndef HAS_VALGRIND
			std::abort();
#else
			CALLGRIND_START_INSTRUMENTATION;

			static_cast<ProfilerImpl*>(this)->RunImpl();

			CALLGRIND_STOP_INSTRUMENTATION;
			CALLGRIND_DUMP_STATS;
#endif
		}

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

		inline const auto& GetPerformance() const noexcept { return _performance; }
		inline double GetAverage() const noexcept { return GetPerformance().GetAverage(); }
		inline double GetStandardDeviation() const noexcept { return GetPerformance().GetStandardDeviation(); }
		inline const auto& GetPercentiles() const noexcept { return GetPerformance().GetPercentiles(); }
		inline const auto& GetRawObservations() const noexcept { return GetPerformance().GetRawObservations(); }

		inline std::string ToString() const noexcept
		{
			std::string ret = _config.ToString();
			ret += _performance.ToString();
			return ret;
		}
		inline void Print() const noexcept { std::cout << ToString() << std::endl; }

		inline std::string PercentilesToCsv(const bool includePercentileKeys = false) const noexcept
		{
			std::string ret {};

			if (includePercentileKeys)
			{
				for (size_t i = 0; i < detail::nPercentiles; ++i)
					ret += std::to_string(detail::percentiles[i]) + ",";
			}
			ret += "\n";

			for (size_t i = 0; i < detail::nPercentiles; ++i)
				ret += std::to_string(_performance.GetPercentiles()[i]) + ",";
			ret += "\n";

			return ret;
		}
		inline void PrintPercentilesCsv() const noexcept { std::cout << PercentilesToCsv() << std::endl; }

		inline std::string AverageToString() const noexcept
		{
			const double mid = _performance.GetAverage();
			const double lo = mid - _performance.GetStandardDeviation();
			const double hi = mid + _performance.GetStandardDeviation();
			return std::string("Average: ") + std::to_string(mid) + "[ " + std::to_string(lo) + ", " + std::to_string(hi) + " ]";
		}
		inline void PrintAverage() const noexcept { std::cout << AverageToString() << std::endl; }

		inline std::string ToPythonPlot(bool show, const std::string& label = "", bool includeRawObservations = false) const noexcept
		{
			const auto& percentiles = _performance.GetPercentiles();
			const auto& rawObservations = _performance.GetRawObservations();

			std::string ret {};
			ret += "import matplotlib.pyplot as plt;";
			ret += "import numpy as np;";

			ret += "percentiles=[";
			for (double percentile : detail::percentiles)
				ret += std::to_string(percentile) + ",";
			ret += "];";

			ret += "percentileValues=[";
			for (double percentile : percentiles)
				ret += std::to_string(percentile) + ",";
			ret += "];";

			if (includeRawObservations)
			{
				ret += "observations=[";
				for (double observation : rawObservations)
					ret += std::to_string(observation) + ",";
				ret += "];";

				ret += "fig = plt.figure(1);";
				ret += "ax = fig.add_subplot(121);";
				ret += "n,bins,patches=ax.hist(x=observations,bins='auto',color='#0504aa',alpha=0.7);";
				ret += "ax.grid(alpha=0.75);";
				ret += "max_freq=n.max();";
				ret += "ax.set_ylim(ymax=np.ceil(max_freq/10)*10 if max_freq%10 else max_freq+10);";

				ret += "ax = fig.add_subplot(122);";
				ret += "ax.plot(percentiles, percentileValues, label='" + label + "');";
				ret += "ax.grid(alpha=0.75);";
				ret += "ax.legend(loc='best')";
			}
			else
			{
				ret += "fig = plt.figure(1);";
				ret += "ax = fig.add_subplot(111);";
				ret += "ax.plot(percentiles, percentileValues, label='" + label + "');";
				ret += "ax.grid(alpha=0.75);";
				ret += "ax.legend(loc='lower right');";
			}

			if (show)
				ret += "plt.show();";

			return ret;
		}
		inline void PrintPythonPlotInstructions(bool show, const std::string& label = "", bool includeRawObservations = false)
		{
			std::cout << ToPythonPlot(show, label, includeRawObservations) << std::endl;
		}

	protected:
		const ConfigImpl _config;

	private:
		detail::Performance _performance { _config.nIterations };
	};
}	 // namespace perf
