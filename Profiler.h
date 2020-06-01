
#pragma once

#include <StopWatch.h>

#ifdef HAS_VALGRIND
	#include <valgrind/callgrind.h>
#endif

#include <cstddef>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace perf
{
	namespace detail
	{
		static constexpr size_t nPercentiles = 13;
		using Percentiles = std::array<double, nPercentiles>;
		static constexpr Percentiles percentiles = {{ 1.0, 5.0, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 95.0, 99.0, }};
		static constexpr size_t medianIndex = { 6 };

		class Performance
		{
		public:
			explicit Performance(size_t nObservations) noexcept
			{
				Reset(nObservations);
			}

			inline void Reset(size_t nObservations) noexcept
			{
				_observations.clear();
				_observations.reserve(nObservations);

				_average = 0.0;
				_standardDeviation = 0.0;
				_percentiles.fill(0.0);
			}

			inline void Register(double observation) noexcept { _observations.emplace_back(observation); }

			inline void ComputeAnalytics() noexcept
			{
				ComputePercentiles();
				ComputeAverage();
				ComputeStandardDeviation();
			}

			inline auto GetAverage() const noexcept { return _average; }
			inline auto GetStandardDeviation() const noexcept { return _standardDeviation; }
			inline auto GetMedian() const noexcept { return _percentiles[medianIndex]; }
			inline const auto& GetPercentiles() const noexcept { return _percentiles; }
			inline const auto& GetRawObservations() const noexcept { return _observations; }

			inline std::string ToString(const bool printRawObservations = false) const noexcept
			{
				std::string ret {};

				ret += "Average           : \t" + std::to_string(_average) + "\n";
				ret += "Median            : \t" + std::to_string(GetMedian()) + "\n";
				ret += "Standard Deviation: \t" + std::to_string(_standardDeviation) + "\n";
				ret += "Percentiles:\n-----------------\n";
				for (size_t i = 0; i < nPercentiles; ++i)
					ret += std::to_string(percentiles[i]) + "\t%  :\t" + std::to_string(_percentiles[i]) + "\n";
				ret += "-----------------\n";
				if (printRawObservations)
				{
					ret += "Observations:\n-----------------\n";
					for (size_t i = 0; i < _observations.size(); ++i)
						ret += std::to_string(i) + "  \t:\t" + std::to_string(_observations[i]) + "\n";
					ret += "-----------------\n";
				}

				return ret;
			}
			inline void Print(const bool printRawObservations = false) const noexcept { std::cout << ToString(printRawObservations) << std::endl; }

		private:
			inline void ComputeAverage() noexcept
			{
				_average = 0.0;
				for (auto observation: _observations)
					_average += observation;
				_average /= static_cast<double>(_observations.size());
			}

			inline void ComputeStandardDeviation() noexcept
			{
				_standardDeviation = 0.0;
				for (auto observation: _observations)
					_standardDeviation += observation * observation;
				_standardDeviation /= static_cast<double>(_observations.size());
				_standardDeviation -= _average * _average;
				_standardDeviation = std::sqrt(_standardDeviation);
			}

			inline void ComputePercentiles() noexcept
			{
				std::sort(_observations.begin(), _observations.end());
				for (size_t i = 0; i < nPercentiles; ++i)
				{
					const double fractionalIndex = (percentiles[i] / 100.0) * (static_cast<double>(_observations.size()) - 1.0);
					const auto observationIndex = static_cast<size_t>(fractionalIndex);

					if (observationIndex < _observations.size() - 1)
					{
						const double weight = fractionalIndex - static_cast<double>(observationIndex);
						_percentiles[i] = _observations[observationIndex] * (1.0 - weight) + weight * _observations[observationIndex + 1];
					}
					else
						_percentiles[i] = _observations[observationIndex];
				}
			}

		private:
			std::vector<double> _observations {};
			Percentiles _percentiles {};

			double _average = 0.0;
			double _standardDeviation = 0.0;
		};
	}

	enum class TimeScale
	{
		Nanoseconds,
		Microseconds,
		Milliseconds,
		Seconds,
	};
	static inline std::string ToString(const TimeScale timeScale) noexcept
	{
		switch (timeScale)
		{
			case TimeScale::Nanoseconds:
				return "Nanoseconds";
			case TimeScale::Microseconds:
				return "Microseconds";
			case TimeScale::Milliseconds:
				return "Milliseconds";
			case TimeScale::Seconds:
				return "Seconds";
			default:
				return "?";
		}
	}

	struct Config
	{
		size_t nIterations = 100;
		size_t nIterationsPerCycle = 1;
		size_t nWarmUpIterations = 1;
		TimeScale timeScale = TimeScale::Nanoseconds;

		inline std::string ToString() const noexcept
		{
			std::string ret {};

			ret += "#Iterations           : \t" + std::to_string(nIterations) + "\n";
			ret += "#Iterations Per Cycle : \t" + std::to_string(nIterationsPerCycle) + "\n";
			ret += "#WarmUp Iterations    : \t" + std::to_string(nWarmUpIterations) + "\n";
			ret += "TimeScale             : \t" + ::perf::ToString(timeScale) + "\n";

			return ret;
		}
		inline void Print() const noexcept { std::cout << ToString() << std::endl; }
	};

	template<typename ProfilerImpl, typename ConfigImpl>
	class Profiler
	{
	public:
		explicit Profiler(const ConfigImpl& config) noexcept
			: _config(config)
		{
		}

		inline void Profile() noexcept
		{
			OnStart();

			// warm up caches
			for (size_t i = 0; i < _config.nWarmUpIterations; ++i)
				static_cast<ProfilerImpl*>(this)->RunImpl();

			Stopwatch stopWatch(false);
			const auto timeScaleMultiplier = GetTimeScaleMultiplier();
			for (size_t i = 0; i < _config.nIterations; ++i)
			{
				stopWatch.Start();
				for (size_t j = 0; j < _config.nIterationsPerCycle; ++j)
					static_cast<ProfilerImpl*>(this)->RunImpl();
				stopWatch.Stop();

				const auto elapsedTime = static_cast<double>(stopWatch.GetNanoSeconds()) / static_cast<double>(_config.nIterationsPerCycle);
				_performance.Register(elapsedTime * timeScaleMultiplier);
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
			std::string ret{};

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

		inline std::string AverageToString() const noexcept { return "Average: " + std::to_string(_performance.GetAverage()); }
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
		inline void PrintPythonPlotInstructions(bool show, const std::string& label = "", bool includeRawObservations = false) { std::cout << ToPythonPlot(show, label, includeRawObservations) << std::endl; }

	private:
		inline double GetTimeScaleMultiplier() const noexcept
		{
			switch (_config.timeScale)
			{
				case TimeScale::Nanoseconds:
					return 1.0;
				case TimeScale::Microseconds:
					return 1e-3;
				case TimeScale::Milliseconds:
					return 1e-6;
				case TimeScale::Seconds:
					return 1e-9;
				default:
					return std::numeric_limits<double>::quiet_NaN();
			}
		}

	protected:
		const ConfigImpl _config;
	private:
		detail::Performance _performance { _config.nIterations };
	};
}
