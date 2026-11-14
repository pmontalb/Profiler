#include "Profiler.h"

#include "IgnoreWarning.h"

#ifdef HAS_VALGRIND
	#include <valgrind/callgrind.h>
#endif
#ifdef HAS_VTUNE
	#include <ittnotify.h>
#endif

#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <iterator>
#include <limits>
#include <unistd.h>

namespace perf::detail
{
	void Performance::Reset(const std::size_t nObservations) noexcept
	{
		_observations.clear();
		_observations.reserve(nObservations);

		_average = 0.0;
		_standardDeviation = 0.0;
		_percentiles.fill(0.0);
		_scale = TimeScale::Nanoseconds;
		_timeScaleMultiplier = 1.0;
	}

	void Performance::ComputeAnalytics() noexcept
	{
		if (_observations.empty())
		{
			_average = 0.0;
			_standardDeviation = 0.0;
			_percentiles.fill(0.0);
			_scale = TimeScale::Nanoseconds;
			_timeScaleMultiplier = 1.0;
			return;
		}

		ComputeMeanAndStdDev();
		ComputePercentiles();
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
		for (std::size_t i = 0; i < nPercentiles; ++i)
			ret["percentiles"][std::to_string(percentiles[i])] = _percentiles[i];
		ret["observations"] = _observations;

		return ret;
	}

	std::string Performance::ToString(const bool printPercentiles, const bool printRawObservations) const noexcept
	{
		const auto unit = perf::ToString(_scale);
		std::string ret {};
		ret.reserve(256 + (printPercentiles ? nPercentiles * 40 : 0) + (printRawObservations ? _observations.size() * 32 : 0));

		ret += "Average           : \t" + std::to_string(GetAverage()) + " " + unit + "\n";
		ret += "Median            : \t" + std::to_string(GetMedian()) + " " + unit + "\n";
		ret += "Standard Deviation: \t" + std::to_string(GetStandardDeviation()) + " " + unit + "\n";
		if (printPercentiles)
		{
			ret += "Percentiles:\n-----------------\n";
			for (std::size_t i = 0; i < nPercentiles; ++i)
				ret += std::to_string(percentiles[i]) + "\t%  :\t" + std::to_string(_percentiles[i] * _timeScaleMultiplier) + " " + unit + "\n";
			ret += "-----------------\n";
		}
		if (printRawObservations)
		{
			ret += "Observations:\n-----------------\n";
			for (std::size_t i = 0; i < _observations.size(); ++i)
				ret += std::to_string(i) + "  \t:\t" + std::to_string(_observations[i] * _timeScaleMultiplier) + " " + unit + "\n";
			ret += "-----------------\n";
		}

		return ret;
	}

	void Performance::ComputeMeanAndStdDev() noexcept
	{
		auto mean = 0.0;
		auto m2 = 0.0;
		std::size_t count = 0;

		for (const auto observation : _observations)
		{
			++count;
			const auto delta = observation - mean;
			mean += delta / static_cast<double>(count);
			const auto delta2 = observation - mean;
			m2 += delta * delta2;
		}

		_average = mean;
		_standardDeviation = std::sqrt(std::max(0.0, m2) / static_cast<double>(count));
	}

	void Performance::ComputePercentiles() noexcept
	{
		auto sorted = _observations;
		std::sort(sorted.begin(), sorted.end());

		const auto lastIndex = sorted.size() - 1;
		for (std::size_t i = 0; i < nPercentiles; ++i)
		{
			const auto fractionalIndex = (percentiles[i] / 100.0) * static_cast<double>(lastIndex);
			const auto observationIndex = static_cast<std::size_t>(fractionalIndex);

			if (observationIndex < lastIndex)
			{
				const auto weight = fractionalIndex - static_cast<double>(observationIndex);
				_percentiles[i] = sorted[observationIndex] * (1.0 - weight) + weight * sorted[observationIndex + 1];
			}
			else
			{
				_percentiles[i] = sorted[observationIndex];
			}
		}
	}

	void Performance::ComputeMostFrequentTimeScale() noexcept
	{
		std::array<std::size_t, static_cast<std::size_t>(TimeScale::COUNT)> counters {};

		for (const auto& observation : _observations)
		{
			if (observation >= 1e9)
				++counters[static_cast<std::size_t>(TimeScale::Seconds)];
			else if (observation >= 1e6)
				++counters[static_cast<std::size_t>(TimeScale::Milliseconds)];
			else if (observation >= 1e3)
				++counters[static_cast<std::size_t>(TimeScale::Microseconds)];
			else
				++counters[static_cast<std::size_t>(TimeScale::Nanoseconds)];
		}

		const auto distance = std::distance(counters.begin(), std::max_element(counters.begin(), counters.end()));
		assert(distance >= 0);
		assert(distance < static_cast<std::ptrdiff_t>(TimeScale::COUNT));
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

namespace perf
{
	namespace
	{
		[[nodiscard]] int ParseFdEnv(const char* envName) noexcept
		{
			const auto* const value = std::getenv(envName);
			if (value == nullptr || value[0] == '\0')
				return -1;

			int fd = -1;
			const auto* const end = value + std::strlen(value);
			const auto [ptr, ec] = std::from_chars(value, end, fd);
			if (ec != std::errc {} || ptr != end || fd < 0)
				return -1;

			return fd;
		}
	}	 // namespace

	PerfControl::PerfControl(const char* ctlFifoPath, const char* ackFifoPath) noexcept
	{
		if (ctlFifoPath != nullptr && ctlFifoPath[0] != '\0')
		{
			_ctlFd = ::open(ctlFifoPath, O_WRONLY);
			_ownsCtlFd = (_ctlFd >= 0);

			if (ackFifoPath != nullptr && ackFifoPath[0] != '\0')
			{
				_ackFd = ::open(ackFifoPath, O_RDONLY);
				_ownsAckFd = (_ackFd >= 0);
			}
			return;
		}

		const auto ctlFdEnv = ParseFdEnv("PERF_CTL_FD");
		if (ctlFdEnv >= 0)
		{
			_ctlFd = ctlFdEnv;
			_ackFd = ParseFdEnv("PERF_ACK_FD");
			return;
		}

		const auto* const ctlFifoEnv = std::getenv("PERF_CTL_FIFO");
		if (ctlFifoEnv != nullptr && ctlFifoEnv[0] != '\0')
		{
			_ctlFd = ::open(ctlFifoEnv, O_WRONLY);
			_ownsCtlFd = (_ctlFd >= 0);

			const auto* const ackFifoEnv = std::getenv("PERF_ACK_FIFO");
			if (ackFifoEnv != nullptr && ackFifoEnv[0] != '\0')
			{
				_ackFd = ::open(ackFifoEnv, O_RDONLY);
				_ownsAckFd = (_ackFd >= 0);
			}
		}
	}

	PerfControl::~PerfControl() noexcept
	{
		if (_enabled)
			Disable();

		if (_ownsCtlFd && _ctlFd >= 0)
			::close(_ctlFd);
		if (_ownsAckFd && _ackFd >= 0)
			::close(_ackFd);
	}

	bool PerfControl::Enable() noexcept
	{
		if (_ctlFd < 0 || _enabled)
			return false;

		static constexpr char enableCmd[] = "enable\n";
		if (!SendCommand(enableCmd, sizeof(enableCmd) - 1))
			return false;

		if (!WaitAck())
			return false;

		_enabled = true;
		return true;
	}

	bool PerfControl::Disable() noexcept
	{
		if (_ctlFd < 0 || !_enabled)
			return false;

		static constexpr char disableCmd[] = "disable\n";
		const auto sent = SendCommand(disableCmd, sizeof(disableCmd) - 1);
		const auto acked = sent ? WaitAck() : false;
		_enabled = false;
		return sent && acked;
	}

	bool PerfControl::SendCommand(const char* cmd, const std::size_t length) noexcept
	{
		std::size_t totalWritten = 0;
		while (totalWritten < length)
		{
			const auto written = ::write(_ctlFd, cmd + totalWritten, length - totalWritten);
			if (written > 0)
			{
				totalWritten += static_cast<std::size_t>(written);
			}
			else if (written == -1 && errno == EINTR)
			{
				continue;
			}
			else
			{
				return false;
			}
		}
		return true;
	}

	bool PerfControl::WaitAck() noexcept
	{
		if (_ackFd < 0)
			return true;

		char ch = '\0';
		while (true)
		{
			const auto bytesRead = ::read(_ackFd, &ch, 1);
			if (bytesRead == 1)
			{
				if (ch == '\n')
					return true;
			}
			else if (bytesRead == -1 && errno == EINTR)
			{
				continue;
			}
			else
			{
				return false;
			}
		}
	}

	CallgrindScope::CallgrindScope() noexcept
	{
#ifdef HAS_VALGRIND
		IGNORE_WARNING_PUSH()
		IGNORE_WARNING("-Wold-style-cast")
		CALLGRIND_START_INSTRUMENTATION;
		IGNORE_WARNING_POP()
#endif
	}

	CallgrindScope::~CallgrindScope() noexcept
	{
#ifdef HAS_VALGRIND
		IGNORE_WARNING_PUSH()
		IGNORE_WARNING("-Wold-style-cast")
		CALLGRIND_STOP_INSTRUMENTATION;
		CALLGRIND_DUMP_STATS;
		IGNORE_WARNING_POP()
#endif
	}

#ifdef HAS_VTUNE
	VtuneScope::VtuneScope(const char* taskName, const char* domainName) noexcept
	{
		IGNORE_WARNING_PUSH()
		IGNORE_WARNING("-Wzero-as-null-pointer-constant")
		IGNORE_WARNING("-Wold-style-cast")
		_domain = __itt_domain_create(domainName);
		_handle = __itt_string_handle_create(taskName);
		__itt_resume();
		__itt_task_begin(static_cast<__itt_domain*>(_domain), __itt_null, __itt_null, static_cast<__itt_string_handle*>(_handle));
		IGNORE_WARNING_POP()
	}
#else
	VtuneScope::VtuneScope(const char*, const char*) noexcept = default;
#endif

	VtuneScope::~VtuneScope() noexcept
	{
#ifdef HAS_VTUNE
		IGNORE_WARNING_PUSH()
		IGNORE_WARNING("-Wzero-as-null-pointer-constant")
		IGNORE_WARNING("-Wold-style-cast")
		__itt_task_end(static_cast<__itt_domain*>(_domain));
		__itt_pause();
		IGNORE_WARNING_POP()
#endif
	}

}	 // namespace perf

