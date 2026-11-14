#include <Profiler.h>

#include <cassert>
#include <cstddef>

namespace
{
	class SmokeProfiler : public perf::Profiler<SmokeProfiler, perf::Config>
	{
		friend class perf::Profiler<SmokeProfiler, perf::Config>;

	public:
		using perf::Profiler<SmokeProfiler, perf::Config>::Profiler;

		std::size_t counter = 0;

	private:
		inline void RunImpl() noexcept
		{
			++counter;
			perf::DoNotOptimize(counter);
		}
	};
}	 // namespace

int main()
{
	perf::Config config {};
	config.nIterations = 4;
	config.nIterationsPerCycle = 2;
	config.nWarmUpIterations = 1;

	SmokeProfiler profiler(config);
	profiler.Profile();

	assert(profiler.counter == 9U);
	assert(profiler.GetRawObservations().size() == 4U);
	return 0;
}
