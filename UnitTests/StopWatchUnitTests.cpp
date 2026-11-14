#include <StopWatch.h>

#include <gtest/gtest.h>

#include <sstream>
#include <thread>

TEST(StopWatchTest, DefaultNotStartedReturnsZero)
{
	const sw::StopWatch stopWatch(false);
	EXPECT_DOUBLE_EQ(stopWatch.GetNanoSeconds(), 0.0);
	EXPECT_DOUBLE_EQ(stopWatch.GetMicroSeconds(), 0.0);
	EXPECT_DOUBLE_EQ(stopWatch.GetMilliSeconds(), 0.0);
	EXPECT_DOUBLE_EQ(stopWatch.GetSeconds(), 0.0);
}

TEST(StopWatchTest, MeasuresPositiveDurationAndUnitRatios)
{
	sw::StopWatch stopWatch(false);
	stopWatch.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	stopWatch.Stop();

	const auto ns = stopWatch.GetNanoSeconds();
	EXPECT_GT(ns, 1'000'000.0);
	EXPECT_NEAR(stopWatch.GetMicroSeconds(), ns * 1e-3, 1e-9 * ns + 1e-9);
	EXPECT_NEAR(stopWatch.GetMilliSeconds(), ns * 1e-6, 1e-12 * ns + 1e-9);
	EXPECT_NEAR(stopWatch.GetSeconds(), ns * 1e-9, 1e-15 * ns + 1e-12);
}

TEST(StopWatchTest, ResetClearsPreviousMeasurement)
{
	sw::StopWatch stopWatch(false);
	stopWatch.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(2));
	stopWatch.Stop();
	EXPECT_GT(stopWatch.GetNanoSeconds(), 0.0);

	stopWatch.Reset(false);
	EXPECT_DOUBLE_EQ(stopWatch.GetNanoSeconds(), 0.0);

	stopWatch.Reset(true);
	EXPECT_DOUBLE_EQ(stopWatch.GetNanoSeconds(), 0.0);
	std::this_thread::sleep_for(std::chrono::milliseconds(1));
	stopWatch.Stop();
	EXPECT_GT(stopWatch.GetNanoSeconds(), 0.0);
}

TEST(StopWatchTest, StreamFormattingIncludesUnit)
{
	const sw::StopWatch stopWatch(false);
	std::ostringstream oss {};
	oss << stopWatch;
	EXPECT_EQ(oss.str(), "0ns");
}

#ifdef __x86_64__
TEST(TscStopWatchTest, DefaultNotStartedAndMeasuresTicksAndDuration)
{
	const sw::TscStopWatch emptyWatch(false);
	EXPECT_EQ(emptyWatch.GetTicks(), 0U);
	EXPECT_DOUBLE_EQ(emptyWatch.GetNanoSeconds(), 0.0);

	EXPECT_GT(sw::TscStopWatch::GetNsPerTick(), 0.0);
	EXPECT_GT(sw::TscStopWatch::GetFrequencyHz(), 1e8);

	sw::TscStopWatch tscWatch(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	tscWatch.Stop();

	EXPECT_GT(tscWatch.GetTicks(), 0U);
	const auto ns = tscWatch.GetNanoSeconds();
	EXPECT_GT(ns, 1'000'000.0);
	EXPECT_NEAR(tscWatch.GetMicroSeconds(), ns * 1e-3, 1e-9 * ns + 1e-9);
	EXPECT_NEAR(tscWatch.GetMilliSeconds(), ns * 1e-6, 1e-12 * ns + 1e-9);
	EXPECT_NEAR(tscWatch.GetSeconds(), ns * 1e-9, 1e-15 * ns + 1e-12);

	tscWatch.Reset(false);
	EXPECT_EQ(tscWatch.GetTicks(), 0U);
	EXPECT_DOUBLE_EQ(tscWatch.GetNanoSeconds(), 0.0);
}
#endif

TEST(ChronoStopWatchTest, DefaultNotStartedAndMeasuresDuration)
{
	const sw::ChronoStopWatch<> emptyWatch(false);
	EXPECT_DOUBLE_EQ(emptyWatch.GetNanoSeconds(), 0.0);

	sw::ChronoStopWatch<> chronoWatch(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	chronoWatch.Stop();

	const auto ns = chronoWatch.GetNanoSeconds();
	EXPECT_GT(ns, 1'000'000.0);
	EXPECT_NEAR(chronoWatch.GetMicroSeconds(), ns * 1e-3, 1e-9 * ns + 1e-9);
	EXPECT_NEAR(chronoWatch.GetMilliSeconds(), ns * 1e-6, 1e-12 * ns + 1e-9);
	EXPECT_NEAR(chronoWatch.GetSeconds(), ns * 1e-9, 1e-15 * ns + 1e-12);

	std::ostringstream oss {};
	oss << chronoWatch;
	EXPECT_NE(oss.str().find("ms"), std::string::npos);

	chronoWatch.Reset(false);
	EXPECT_DOUBLE_EQ(chronoWatch.GetNanoSeconds(), 0.0);
}

namespace
{
	struct FakeClock
	{
		using rep = std::chrono::nanoseconds::rep;
		using period = std::nano;
		using duration = std::chrono::nanoseconds;
		using time_point = std::chrono::time_point<FakeClock>;

		static inline time_point current {};
		static time_point now() noexcept { return current; }
	};
}	 // namespace

TEST(ChronoStopWatchTest, SupportsDeterministicCustomClock)
{
	FakeClock::current = FakeClock::time_point(std::chrono::nanoseconds(1'000));
	sw::ChronoStopWatch<FakeClock> watch(true);
	FakeClock::current += std::chrono::nanoseconds(12'500);
	watch.Stop();

	EXPECT_DOUBLE_EQ(watch.GetNanoSeconds(), 12'500.0);
	EXPECT_DOUBLE_EQ(watch.GetMicroSeconds(), 12.5);
	EXPECT_DOUBLE_EQ(watch.GetMilliSeconds(), 0.0125);
}
