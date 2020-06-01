
#include <Profiler.h>

#include <array>
#include <iostream>

static inline void expensiveFunction(std::array<double, 1000>& ret, const double seed)
{
	ret[0] = 1.0;
	for (size_t i = 1; i < ret.size(); ++i)
		ret[i] = std::exp(-std::fabs(seed * ret[i - 1]));
}

struct ExampleConfig1: public perf::Config {};
class Example1: public perf::Profiler<Example1, ExampleConfig1>
{
	friend class perf::Profiler<Example1, ExampleConfig1>;
public:
	using perf::Profiler<Example1, ExampleConfig1>::Profiler;
private:
	inline void OnStartImpl() noexcept
	{
		std::cout << "starting..." << std::endl;
	}

	inline void OnEndImpl() noexcept
	{
		std::cout << "done! " << std::endl;
	}

	inline void RunImpl() noexcept
	{
		cache.fill(0.0);
		expensiveFunction(cache, 1.0);
	}

private:
	std::array<double, 1000> cache {};
};

struct ExampleConfig2: public perf::Config
{
	double seed = 0.1234;
};
class Example2: public perf::Profiler<Example2, ExampleConfig2>
{
	friend class perf::Profiler<Example2, ExampleConfig2>;
public:
	using perf::Profiler<Example2, ExampleConfig2>::Profiler;
private:

	inline void OnStartImpl() noexcept { }

	inline void OnEndImpl() noexcept { }

	inline void RunImpl() noexcept
	{
		cache.fill(0.0);
		expensiveFunction(cache, _config.seed);
	}

private:
	std::array<double, 1000> cache {};
};


int main()
{
	ExampleConfig1 config1 {};
	config1.nIterations = 10;
	config1.nIterationsPerCycle = 1;
	config1.nWarmUpIterations = 1;
	config1.timeScale = perf::TimeScale::Microseconds;
	Example1 profiler1(config1);
	profiler1.Profile();
	profiler1.Print();

	ExampleConfig2 config2 {};
	config2.nIterations = 300;
	config2.nIterationsPerCycle = 1;
	config2.nWarmUpIterations = 10;
	config2.timeScale = perf::TimeScale::Microseconds;
	Example2 profiler2(config2);
	profiler2.Profile();
	profiler2.AverageToString();
	profiler2.PrintPercentilesCsv();
	profiler2.PrintPythonPlotInstructions(true, "", true);

#ifdef HAS_VALGRIND
	// run with `valgrind --tool=callgrind --instr-atstart=no ./example`
	profiler2.Instrument();
#endif

	return 0;
}
