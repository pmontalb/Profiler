#include <Profiler.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{
	class MinimalProfiler : public perf::Profiler<MinimalProfiler, perf::Config>
	{
		friend class perf::Profiler<MinimalProfiler, perf::Config>;

	public:
		using perf::Profiler<MinimalProfiler, perf::Config>::Profiler;

		std::size_t runCount = 0;

	private:
		inline void RunImpl() noexcept
		{
			++runCount;
			perf::DoNotOptimize(runCount);
			perf::ClobberMemory();
		}
	};

	class HookCountingProfiler : public perf::Profiler<HookCountingProfiler, perf::Config>
	{
		friend class perf::Profiler<HookCountingProfiler, perf::Config>;

	public:
		using perf::Profiler<HookCountingProfiler, perf::Config>::Profiler;

		std::size_t onStartCount = 0;
		std::size_t onEndCount = 0;
		std::size_t preRunCount = 0;
		std::size_t postRunCount = 0;
		std::size_t runCount = 0;

	private:
		inline void OnStartImpl() noexcept { ++onStartCount; }
		inline void OnEndImpl() noexcept { ++onEndCount; }
		inline void PreRunImpl() noexcept { ++preRunCount; }
		inline void PostRunImpl() noexcept { ++postRunCount; }
		inline void RunImpl() noexcept
		{
			++runCount;
			perf::DoNotOptimize(runCount);
		}
	};
}	 // namespace

TEST(ProfilerTest, DefaultHooksCompileAndRunExactIterationCounts)
{
	perf::Config config {};
	config.nIterations = 5;
	config.nIterationsPerCycle = 3;
	config.nWarmUpIterations = 2;

	MinimalProfiler profiler(config);
	profiler.Profile();

	EXPECT_EQ(profiler.runCount, 2U + 5U * 3U);
	EXPECT_EQ(profiler.GetRawObservations().size(), 5U);
}

TEST(ProfilerTest, LifecycleHooksCalledExpectedNumberOfTimes)
{
	perf::Config config {};
	config.nIterations = 4;
	config.nIterationsPerCycle = 3;
	config.nWarmUpIterations = 2;

	HookCountingProfiler profiler(config);
	profiler.Profile();

	EXPECT_EQ(profiler.onStartCount, 1U);
	EXPECT_EQ(profiler.onEndCount, 1U);
	EXPECT_EQ(profiler.preRunCount, 4U);
	EXPECT_EQ(profiler.postRunCount, 4U);
	EXPECT_EQ(profiler.runCount, 2U + 4U * 3U);
}

TEST(ProfilerTest, ZeroIterationsHandledSafelyWithoutNaNOrCrash)
{
	perf::Config config {};
	config.nIterations = 0;
	config.nIterationsPerCycle = 1;
	config.nWarmUpIterations = 0;

	MinimalProfiler profiler(config);
	profiler.Profile();

	EXPECT_TRUE(profiler.GetRawObservations().empty());
	EXPECT_DOUBLE_EQ(profiler.GetAverage(), 0.0);
	EXPECT_DOUBLE_EQ(profiler.GetStandardDeviation(), 0.0);
	EXPECT_DOUBLE_EQ(profiler.GetMedian(), 0.0);
	EXPECT_EQ(profiler.GetTimeScale(), perf::TimeScale::Nanoseconds);
	EXPECT_FALSE(std::isnan(profiler.GetAverage()));
	EXPECT_FALSE(std::isnan(profiler.GetStandardDeviation()));
}

TEST(PerformanceTest, ConstantObservationsYieldZeroStdDevWithoutNaN)
{
	perf::detail::Performance performance(100);
	for (std::size_t i = 0; i < 100; ++i)
		performance.Register(12345.6789);
	performance.ComputeAnalytics();

	EXPECT_EQ(performance.GetTimeScale(), perf::TimeScale::Microseconds);
	EXPECT_NEAR(performance.GetAverage(), 12.3456789, 1e-12);
	EXPECT_NEAR(performance.GetMedian(), 12.3456789, 1e-12);
	EXPECT_DOUBLE_EQ(performance.GetStandardDeviation(), 0.0);
	EXPECT_FALSE(std::isnan(performance.GetStandardDeviation()));
}

TEST(PerformanceTest, PreservesChronologicalOrderAndComputesExactStatistics)
{
	perf::detail::Performance performance(5);
	// Out-of-order microseconds-range observations (in nanoseconds)
	performance.Register(5'000.0);
	performance.Register(1'000.0);
	performance.Register(3'000.0);
	performance.Register(2'000.0);
	performance.Register(4'000.0);
	performance.ComputeAnalytics();

	const auto& raw = performance.GetRawObservations();
	ASSERT_EQ(raw.size(), 5U);
	EXPECT_DOUBLE_EQ(raw[0], 5'000.0);
	EXPECT_DOUBLE_EQ(raw[1], 1'000.0);
	EXPECT_DOUBLE_EQ(raw[2], 3'000.0);
	EXPECT_DOUBLE_EQ(raw[3], 2'000.0);
	EXPECT_DOUBLE_EQ(raw[4], 4'000.0);

	EXPECT_EQ(performance.GetTimeScale(), perf::TimeScale::Microseconds);
	EXPECT_DOUBLE_EQ(performance.GetAverage(), 3.0);
	EXPECT_DOUBLE_EQ(performance.GetMedian(), 3.0);
	// Population stdev of {1,2,3,4,5} is sqrt(2)
	EXPECT_NEAR(performance.GetStandardDeviation(), std::sqrt(2.0), 1e-12);

	const auto scaledPercentiles = performance.GetPercentiles();
	EXPECT_DOUBLE_EQ(scaledPercentiles[perf::detail::medianIndex], 3.0);
	EXPECT_NEAR(scaledPercentiles.front(), 1.04, 1e-12);
	EXPECT_NEAR(scaledPercentiles.back(), 4.96, 1e-12);

	const auto json = performance.ToJson();
	EXPECT_EQ(json["scale"], "ns");
	EXPECT_DOUBLE_EQ(json["average"].get<double>(), 3'000.0);
	EXPECT_DOUBLE_EQ(json["median"].get<double>(), 3'000.0);
	EXPECT_NEAR(json["stdev"].get<double>(), std::sqrt(2.0) * 1'000.0, 1e-9);
	EXPECT_EQ(json["observations"].size(), 5U);
	EXPECT_DOUBLE_EQ(json["observations"][0].get<double>(), 5'000.0);
}

TEST(ProfilerTest, OutputHelpersFormatCleanly)
{
	perf::Config config {};
	config.nIterations = 3;
	config.nIterationsPerCycle = 1;
	config.nWarmUpIterations = 0;

	MinimalProfiler profiler(config);
	profiler.Profile();

	const auto summary = profiler.ToString(true, true);
	EXPECT_NE(summary.find("Percentiles:"), std::string::npos);
	EXPECT_NE(summary.find("Observations:"), std::string::npos);

	const auto avg = profiler.AverageToString();
	EXPECT_NE(avg.find(perf::ToString(profiler.GetTimeScale())), std::string::npos);

	const auto csvNoKeys = profiler.PercentilesToCsv(false);
	EXPECT_FALSE(csvNoKeys.empty());
	EXPECT_NE(csvNoKeys.front(), '\n');
	EXPECT_EQ(csvNoKeys.find(",\n"), std::string::npos);

	const auto plotScript = profiler.ToPythonPlot(true, "it's fast", true);
	EXPECT_EQ(plotScript.find(")plt.show()"), std::string::npos);
	EXPECT_NE(plotScript.find(";plt.show();"), std::string::npos);
	EXPECT_NE(plotScript.find("it\\'s fast"), std::string::npos);
}

#ifdef __x86_64__
TEST(ProfilerTest, SupportsTscStopWatchBackend)
{
	class TscProfiler : public perf::Profiler<TscProfiler, perf::Config, sw::TscStopWatch>
	{
		friend class perf::Profiler<TscProfiler, perf::Config, sw::TscStopWatch>;

	public:
		using perf::Profiler<TscProfiler, perf::Config, sw::TscStopWatch>::Profiler;

	private:
		inline void RunImpl() noexcept
		{
			auto sum = 0.0;
			for (std::size_t i = 0; i < 64; ++i)
				sum += static_cast<double>(i);
			perf::DoNotOptimize(sum);
		}
	};

	perf::Config config {};
	config.nIterations = 10;
	config.nIterationsPerCycle = 5;
	config.nWarmUpIterations = 2;

	TscProfiler profiler(config);
	profiler.Profile();

	EXPECT_EQ(profiler.GetRawObservations().size(), 10U);
	EXPECT_GE(profiler.GetAverage(), 0.0);
}
#endif

TEST(ProfilerTest, InstrumentPerfSendsEnableAndDisableCommandsOverControlFds)
{
	int ctlPipe[2] { -1, -1 };
	int ackPipe[2] { -1, -1 };
	ASSERT_EQ(::pipe(ctlPipe), 0);
	ASSERT_EQ(::pipe(ackPipe), 0);

	std::string receivedCommands {};
	std::thread fakePerf([&]() {
		auto readCommand = [&]() -> std::string {
			std::string line {};
			char ch = '\0';
			while (::read(ctlPipe[0], &ch, 1) == 1)
			{
				line += ch;
				if (ch == '\n')
					break;
			}
			return line;
		};

		const auto cmd1 = readCommand();
		receivedCommands += cmd1;
		static constexpr char ackMsg[] = "ack\n";
		static constexpr std::size_t ackLen = sizeof(ackMsg) - 1;
		ASSERT_EQ(::write(ackPipe[1], ackMsg, ackLen), static_cast<ssize_t>(ackLen));

		const auto cmd2 = readCommand();
		receivedCommands += cmd2;
		ASSERT_EQ(::write(ackPipe[1], ackMsg, ackLen), static_cast<ssize_t>(ackLen));
	});

	const auto ctlEnv = std::to_string(ctlPipe[1]);
	const auto ackEnv = std::to_string(ackPipe[0]);
	::setenv("PERF_CTL_FD", ctlEnv.c_str(), 1);
	::setenv("PERF_ACK_FD", ackEnv.c_str(), 1);

	perf::Config config {};
	HookCountingProfiler profiler(config);
	const auto active = profiler.InstrumentPerf(2, 3);

	::unsetenv("PERF_CTL_FD");
	::unsetenv("PERF_ACK_FD");
	fakePerf.join();

	::close(ctlPipe[0]);
	::close(ctlPipe[1]);
	::close(ackPipe[0]);
	::close(ackPipe[1]);

	EXPECT_TRUE(active);
	EXPECT_EQ(receivedCommands, "enable\ndisable\n");
	EXPECT_EQ(profiler.runCount, 2U + 3U);
	EXPECT_EQ(profiler.preRunCount, 3U);
	EXPECT_EQ(profiler.postRunCount, 3U);
}

TEST(ProfilerTest, SupportsChronoStopWatchBackend)
{
	class ChronoProfiler : public perf::Profiler<ChronoProfiler, perf::Config, sw::ChronoStopWatch<>>
	{
		friend class perf::Profiler<ChronoProfiler, perf::Config, sw::ChronoStopWatch<>>;

	public:
		using perf::Profiler<ChronoProfiler, perf::Config, sw::ChronoStopWatch<>>::Profiler;

	private:
		inline void RunImpl() noexcept
		{
			auto sum = 0.0;
			for (std::size_t i = 0; i < 64; ++i)
				sum += static_cast<double>(i);
			perf::DoNotOptimize(sum);
		}
	};

	perf::Config config {};
	config.nIterations = 10;
	config.nIterationsPerCycle = 5;
	config.nWarmUpIterations = 2;

	ChronoProfiler profiler(config);
	profiler.Profile();

	EXPECT_EQ(profiler.GetRawObservations().size(), 10U);
	EXPECT_GE(profiler.GetAverage(), 0.0);
}
