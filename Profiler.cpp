
#include "Profiler.h"

namespace perf::detail
{
	void Performance::Reset(size_t nObservations) noexcept
	{
		_observations.clear();
		_observations.reserve(nObservations);

		_average = 0.0;
		_standardDeviation = 0.0;
		_percentiles.fill(0.0);
	}

	void Performance::ComputeAnalytics() noexcept
	{
		ComputePercentiles();
		ComputeAverage();
		ComputeStandardDeviation();
		ComputeMostFrequentTimeScale();
	}

	nlohmann::json Performance::ToJson() const noexcept
	{
		nlohmann::json ret;
		ret["scale"] = perf::ToString(TimeScale::Nanoseconds);
		ret["average"] = _average;
		ret["median"] = _percentiles[medianIndex];
		ret["stdev"] = _standardDeviation;
		ret["percentiles"] = {};
		for (size_t i = 0; i < nPercentiles; ++i)
			ret["percentiles"][std::to_string(percentiles[i])] = _percentiles[i];
		ret["observations"] = _observations;

		return ret;
	}
	std::string Performance::ToString(const bool printPercentiles, const bool printRawObservations) const noexcept
	{
		std::string ret {};

		ret += "Average           : \t" + std::to_string(GetAverage()) + " " + perf::ToString(_scale) + "\n";
		ret += "Median            : \t" + std::to_string(GetMedian()) + " " + perf::ToString(_scale) + "\n";
		ret += "Standard Deviation: \t" + std::to_string(GetStandardDeviation()) + " " + perf::ToString(_scale) + "\n";
		if (printPercentiles)
		{
			ret += "Percentiles:\n-----------------\n";
			for (size_t i = 0; i < nPercentiles; ++i)
				ret += std::to_string(percentiles[i]) + "\t%  :\t" + std::to_string(_percentiles[i] * _timeScaleMultiplier) + " " + perf::ToString(_scale) + "\n";
			ret += "-----------------\n";
		}
		if (printRawObservations)
		{
			ret += "Observations:\n-----------------\n";
			for (size_t i = 0; i < _observations.size(); ++i)
				ret += std::to_string(i) + "  \t:\t" + std::to_string(_observations[i] * _timeScaleMultiplier) + " " + perf::ToString(_scale) + "\n";
			ret += "-----------------\n";
		}

		return ret;
	}

	void Performance::ComputeAverage() noexcept
	{
		_average = 0.0;
		for (auto observation : _observations)
			_average += observation;
		_average /= static_cast<double>(_observations.size());
	}

	void Performance::ComputeStandardDeviation() noexcept
	{
		_standardDeviation = 0.0;
		for (auto observation : _observations)
			_standardDeviation += observation * observation;
		_standardDeviation /= static_cast<double>(_observations.size());
		_standardDeviation -= _average * _average;
		_standardDeviation = std::sqrt(_standardDeviation);
	}

	void Performance::ComputePercentiles() noexcept
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
	void Performance::ComputeMostFrequentTimeScale() noexcept
	{
		std::array<size_t, static_cast<size_t>(TimeScale::COUNT)> counters;
		counters.fill(0);

		for (const auto& observation: _observations)
		{
			if (observation > 1e9)
				++counters[static_cast<size_t>(TimeScale::Seconds)];
			else if (observation > 1e6)
				++counters[static_cast<size_t>(TimeScale::Milliseconds)];
			else if (observation > 1e3)
				++counters[static_cast<size_t>(TimeScale::Microseconds)];
			else
				++counters[static_cast<size_t>(TimeScale::Nanoseconds)];
		}

		const auto distance = std::distance(counters.begin(), std::max_element(counters.begin(), counters.end()));
		assert(distance >= 0);
		assert(distance < static_cast<int>(TimeScale::COUNT));
		_scale = static_cast<TimeScale>(distance);
		switch (_scale)
		{
			case TimeScale::Seconds:
				_timeScaleMultiplier = 1e-9;
				break;
			case TimeScale::Milliseconds:
				_timeScaleMultiplier = 1e-6;
				break;
			case TimeScale::Microseconds:
				_timeScaleMultiplier = 1e-3;
				break;
			case TimeScale::Nanoseconds:
				_timeScaleMultiplier = 1.0;
				break;
			default:
				_timeScaleMultiplier = std::numeric_limits<double>::quiet_NaN();
				assert(false);
				break;
		}
	}

}	 // namespace perf::detail

