#include <Profiler.h>

#include <algorithm>
#include <cstddef>
#include <execution>
#include <functional>
#include <iostream>
#include <random>
#include <vector>

struct Config: public perf::Config {};
template<typename ContainerT>
class StlProfiler: public perf::Profiler<StlProfiler<ContainerT>, Config>
{
	using ThisT = StlProfiler<ContainerT>;
	friend class perf::Profiler<ThisT, Config>;
public:
	using perf::Profiler<ThisT, Config>::Profiler;
private:
	inline void OnStartImpl() noexcept { }

	inline void OnEndImpl() noexcept { }

	inline void RunImpl() noexcept
	{
		action(container);
		perf::DoNotOptimize(container);
	}

public:
	std::function<void(ContainerT&)> action {};
	ContainerT container {};
};


int main()
{
	static constexpr std::size_t containerSize = { 512 };

	Config config {};
	config.nIterations = 10000;
	config.nIterationsPerCycle = 5;
	config.nWarmUpIterations = 10;
	StlProfiler<std::vector<double>> profiler(config);

	std::random_device randomDevice {};
	std::mt19937 engine { randomDevice() };
	std::uniform_real_distribution<double> distribution {0.0, 1.0};
	const auto rng = [&distribution, &engine]() { return distribution(engine); };

	profiler.container.resize(containerSize);
	profiler.action = [&rng](auto& target)
	{
		std::generate(std::execution::seq, std::begin(target), std::end(target), rng);
		std::sort(std::execution::seq, std::begin(target), std::end(target));
	};

	profiler.Profile();
	profiler.PrintPythonPlotInstructions(false, "SEQ - " + std::to_string(containerSize), false);

	profiler.action = [&rng](auto& target)
	{
		std::generate(std::execution::par, std::begin(target), std::end(target), rng);
		std::sort(std::execution::par, std::begin(target), std::end(target));
	};

	profiler.Profile();
	profiler.PrintPythonPlotInstructions(false, "PAR - " + std::to_string(containerSize), false);

	profiler.action = [&rng](auto& target)
	{
		std::generate(std::execution::par_unseq, std::begin(target), std::end(target), rng);
		std::sort(std::execution::par_unseq, std::begin(target), std::end(target));
	};

	profiler.Profile();
	profiler.PrintPythonPlotInstructions(true, "PAR VEC - " + std::to_string(containerSize), false);

	return 0;
}
