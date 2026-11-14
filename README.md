[![CI](https://github.com/pmontalb/Profiler/actions/workflows/ci.yml/badge.svg)](https://github.com/pmontalb/Profiler/actions/workflows/ci.yml)

# Profiler
A lightweight **C++20** benchmarking and timing library for Linux built around a monotonic `CLOCK_MONOTONIC_RAW` stopwatch and a zero-overhead CRTP profiler harness.

## Implemented Functionalities
- **High-Resolution StopWatches ([StopWatch.h](StopWatch.h)):**
  - `sw::StopWatch`: `clock_gettime(CLOCK_MONOTONIC_RAW)` using integer `std::timespec` delta arithmetic (avoids floating-point precision loss over long uptimes) with compiler reordering fences (`std::atomic_signal_fence`).
  - `sw::ChronoStopWatch<ClockT>`: header-only `<chrono>` stopwatch (defaults to `std::chrono::steady_clock`; supports any C++ clock type including custom mock clocks).
  - `sw::TscStopWatch` (`x86_64`): low-overhead serializing `lfence; rdtsc` / `rdtscp; lfence` timer calibrated via CPUID leaf `0x15` (with monotonic fallback calibration), exposing both raw `GetTicks()` and nanosecond/microsecond/millisecond/second accessors.
- **CRTP Harness (`perf::Profiler<ProfilerImpl, ConfigImpl, StopWatchT>`):**
  - Parameterizable clock backend (`sw::StopWatch` by default, or `sw::ChronoStopWatch<>` / `sw::TscStopWatch`).
  - Warm-up iterations (`nWarmUpIterations`), outer measurement samples (`nIterations`), and batched inner cycles (`nIterationsPerCycle`).
  - Zero-virtual-overhead hooks: required `RunImpl()` plus optional `OnStartImpl()`, `OnEndImpl()`, `PreRunImpl()`, and `PostRunImpl()` defaults.
  - Compiler optimization barriers (`perf::DoNotOptimize`, `perf::ClobberMemory`) to prevent dead-code elimination or hoisting of benchmarked work.
- **Region Profiling Backends:**
  - **Linux `perf` (`InstrumentPerf` / `perf::PerfScope`):** Sends `enable` / `disable` commands (with optional `ack` synchronization) over `PERF_CTL_FD`/`PERF_ACK_FD` or `PERF_CTL_FIFO`/`PERF_ACK_FIFO` for `perf record --delay=-1 --control=...` and `perf stat --delay=-1 --control=...`.
  - **Intel VTune (`InstrumentVTune` / `perf::VtuneScope`, `-DUSE_VTUNE=ON`):** Uses ITT (`ittnotify.h`) `__itt_resume()`/`__itt_pause()` and named `__itt_task_begin`/`__itt_task_end` regions for `vtune -collect ... -start-paused`.
  - **Valgrind Callgrind (`InstrumentCallgrind` / `Instrument` / `perf::CallgrindScope`, `-DUSE_VALGRIND=ON`):** Controls `CALLGRIND_START_INSTRUMENTATION` / `CALLGRIND_STOP_INSTRUMENTATION` / `CALLGRIND_DUMP_STATS` for `--instr-atstart=no`.
- **Analytics & Export (`perf::detail::Performance`):**
  - Numerically stable single-pass Welford mean and population standard deviation.
  - Interpolated percentiles (`p1`, `p5`, `p10`, `p20`–`p90`, `p95`, `p99`, and median) computed without altering chronological `GetRawObservations()`.
  - Automatic most-frequent time-scale selection (`ns`, `us`, `ms`, `s`) for human-readable reports (`ToString`, `AverageToString`, `PercentilesToCsv`, `ToPythonPlot`), plus full nanosecond JSON serialization (`nlohmann/json`).
- **Build & Tests:** CMake 3.28+ preset integration via `cmake/` ([CmakeUtilities](https://github.com/pmontalb/CmakeUtilities)), GoogleTest unit tests (`ProfilerTests`, `ProfilerMsanSmoke`), examples (`example`, `exampleStl`), and GitHub Actions Linux CI.

## Building & Testing (Linux)
Dependencies: CMake `3.28+`, Ninja, `GoogleTest` (`libgtest-dev`, top-level builds), and optionally `libtbb-dev` (for `exampleStl`), `valgrind` headers (when `-DUSE_VALGRIND=ON`), or Intel VTune (`ittnotify.h` + `libittnotify` via `VTUNE_PROFILER_DIR` when `-DUSE_VTUNE=ON`).

```bash
cmake -S . --preset=gcc-release
cmake --build --preset=gcc-release
ctest --preset=gcc-release --output-on-failure
```

### Region Profiling Examples
```bash
# Linux perf sampling only around the InstrumentedPerf() region
mkfifo perf_ctl.fifo perf_ack.fifo
PERF_CTL_FIFO=perf_ctl.fifo PERF_ACK_FIFO=perf_ack.fifo \
  perf record --delay=-1 --control=fifo:perf_ctl.fifo,perf_ack.fifo ./cmake-build-gcc-release/bin/example

# Intel VTune (requires -DUSE_VTUNE=ON)
vtune -collect hotspots -start-paused ./cmake-build-gcc-release/bin/example

# Valgrind Callgrind (requires -DUSE_VALGRIND=ON)
valgrind --tool=callgrind --instr-atstart=no ./cmake-build-gcc-release/bin/example
```

See [cmake/README.md](cmake/README.md) for all available compiler, sanitizer, LTO, PGO, coverage, and tool presets.

## Sample Usage
```cpp
#include <Profiler.h>

#include <array>
#include <cmath>
#include <cstddef>

class MyProfiler : public perf::Profiler<MyProfiler, perf::Config>
{
	friend class perf::Profiler<MyProfiler, perf::Config>;

public:
	using perf::Profiler<MyProfiler, perf::Config>::Profiler;

private:
	inline void RunImpl() noexcept
	{
		for (std::size_t i = 0; i < _data.size(); ++i)
			_data[i] = std::sin(static_cast<double>(i));
		perf::DoNotOptimize(_data);
	}

	std::array<double, 1024> _data {};
};

int main()
{
	perf::Config config {};
	config.nIterations = 100;
	config.nIterationsPerCycle = 10;
	config.nWarmUpIterations = 5;

	MyProfiler profiler(config);
	profiler.Profile();
	profiler.Print();
}
```
