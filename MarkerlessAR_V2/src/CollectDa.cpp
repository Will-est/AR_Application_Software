#include "CollectDa.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>

#if COLLECTDA
#include <cstdio>
#include <time.h>
#include <unistd.h>
#endif

namespace collectda
{
#if COLLECTDA
namespace
{
using SteadyClock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;

struct AccumUs
{
    std::uint64_t count = 0;
    std::uint64_t sumUs = 0;
    std::uint64_t maxUs = 0;

    void add(std::uint64_t us)
    {
        ++count;
        sumUs += us;
        if (us > maxUs)
            maxUs = us;
    }

    void reset()
    {
        count = 0;
        sumUs = 0;
        maxUs = 0;
    }
};

#if COLLECTDA
double processCpuSeconds()
{
    timespec ts{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

long readStatusKb(const char* key)
{
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f)
        return -1;

    char line[256];
    const size_t keyLen = std::strlen(key);
    long value = -1;
    while (std::fgets(line, sizeof(line), f))
    {
        if (std::strncmp(line, key, keyLen) == 0)
        {
            // Format: "VmRSS:    12345 kB"
            const char* p = line + keyLen;
            while (*p == ' ' || *p == '\t' || *p == ':')
                ++p;
            value = std::strtol(p, nullptr, 10);
            break;
        }
    }

    std::fclose(f);
    return value;
}

int readStatusInt(const char* key)
{
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f)
        return -1;

    char line[256];
    const size_t keyLen = std::strlen(key);
    int value = -1;
    while (std::fgets(line, sizeof(line), f))
    {
        if (std::strncmp(line, key, keyLen) == 0)
        {
            const char* p = line + keyLen;
            while (*p == ' ' || *p == '\t' || *p == ':')
                ++p;
            value = std::strtol(p, nullptr, 10);
            break;
        }
    }

    std::fclose(f);
    return value;
}
#endif

struct Collector
{
    std::mutex mu;
    std::ofstream out;

    SteadyClock::time_point lastFlush = SteadyClock::now();
    SteadyClock::time_point lastArrival{};
    bool hasLastArrival = false;

    std::uint64_t framesInterval = 0;
    std::uint64_t framesTotal = 0;
    std::uint64_t patternsFoundInterval = 0;

    AccumUs captureDt;
    AccumUs frameProcess;
    AccumUs preprocess;
    AccumUs arrivalToPreprocessEnd;

#if COLLECTDA
    double lastCpuSec = processCpuSeconds();
#endif

    void openIfNeeded()
    {
        if (out.is_open())
            return;

        const char* path = std::getenv("AR_COLLECTDA_PATH");
        const std::string outPath = (path && *path) ? path : "ARProject_collectda.csv";

        bool fileExists = false;
        {
            std::ifstream probe(outPath.c_str());
            fileExists = probe.good();
        }

        out.open(outPath.c_str(), std::ios::out | std::ios::app);
        if (!out.is_open())
            return;

        if (!fileExists)
        {
            out << "epoch_ms,mono_ms,fps,cpu_pct,vmrss_kb,vmsize_kb,threads,"
                   "capture_dt_avg_ms,capture_dt_max_ms,"
                   "frame_proc_avg_ms,frame_proc_max_ms,"
                   "preproc_avg_ms,preproc_max_ms,"
                   "arrival_to_preproc_end_avg_ms,arrival_to_preproc_end_max_ms,"
                   "patterns_found_per_s,frames_total\n";
            out.flush();
        }
    }

    static std::uint64_t epochMs()
    {
        const auto now = SystemClock::now().time_since_epoch();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    static std::uint64_t monoMs()
    {
        const auto now = SteadyClock::now().time_since_epoch();
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    }

    void onArrival()
    {
        const auto now = SteadyClock::now();
        std::lock_guard<std::mutex> lock(mu);
        openIfNeeded();

        ++framesInterval;
        ++framesTotal;

        if (hasLastArrival)
        {
            const auto dtUs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(now - lastArrival).count());
            captureDt.add(dtUs);
        }

        lastArrival = now;
        hasLastArrival = true;
    }

    void onFrameProcessed(std::uint64_t us)
    {
        std::lock_guard<std::mutex> lock(mu);
        frameProcess.add(us);
    }

    void onPattern(bool found)
    {
        if (!found)
            return;
        std::lock_guard<std::mutex> lock(mu);
        ++patternsFoundInterval;
    }

    void onPreprocessDone(std::uint64_t us)
    {
        const auto now = SteadyClock::now();
        std::lock_guard<std::mutex> lock(mu);
        preprocess.add(us);
        if (hasLastArrival)
        {
            const auto dtUs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(now - lastArrival).count());
            arrivalToPreprocessEnd.add(dtUs);
        }
    }

    void maybeFlush()
    {
        const auto now = SteadyClock::now();
        std::lock_guard<std::mutex> lock(mu);
        if (!out.is_open())
            openIfNeeded();
        if (!out.is_open())
            return;

        const auto elapsed = now - lastFlush;
        if (elapsed < std::chrono::seconds(1))
            return;

        const double wallSec = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
        const double fps = wallSec > 0.0 ? (static_cast<double>(framesInterval) / wallSec) : 0.0;

#if COLLECTDA
        const double cpuSecNow = processCpuSeconds();
        const double cpuDelta = cpuSecNow - lastCpuSec;
        const double cpuPct = wallSec > 0.0 ? (100.0 * cpuDelta / wallSec) : 0.0;
        lastCpuSec = cpuSecNow;

        const long vmrssKb = readStatusKb("VmRSS");
        const long vmsizeKb = readStatusKb("VmSize");
        const int threads = readStatusInt("Threads");
#else
        const double cpuPct = 0.0;
        const long vmrssKb = -1;
        const long vmsizeKb = -1;
        const int threads = -1;
#endif

        auto avgMs = [](const AccumUs& acc) -> double
        {
            if (acc.count == 0)
                return 0.0;
            return (static_cast<double>(acc.sumUs) / static_cast<double>(acc.count)) / 1000.0;
        };
        auto maxMs = [](const AccumUs& acc) -> double
        {
            return static_cast<double>(acc.maxUs) / 1000.0;
        };

        const double patternsPerSec = wallSec > 0.0 ? (static_cast<double>(patternsFoundInterval) / wallSec) : 0.0;

        out << epochMs() << ','
            << monoMs() << ','
            << fps << ','
            << cpuPct << ','
            << vmrssKb << ','
            << vmsizeKb << ','
            << threads << ','
            << avgMs(captureDt) << ','
            << maxMs(captureDt) << ','
            << avgMs(frameProcess) << ','
            << maxMs(frameProcess) << ','
            << avgMs(preprocess) << ','
            << maxMs(preprocess) << ','
            << avgMs(arrivalToPreprocessEnd) << ','
            << maxMs(arrivalToPreprocessEnd) << ','
            << patternsPerSec << ','
            << framesTotal << '\n';
        out.flush();

        framesInterval = 0;
        patternsFoundInterval = 0;
        captureDt.reset();
        frameProcess.reset();
        preprocess.reset();
        arrivalToPreprocessEnd.reset();
        lastFlush = now;
    }
};

Collector& collector()
{
    static Collector c;
    return c;
}
} // namespace

void init()
{
    collector().openIfNeeded();
}

void shutdown()
{
    // no-op; rely on destructor flush/close
}

void onFrameArrival()
{
    collector().onArrival();
    collector().maybeFlush();
}

void onFrameProcessedUs(std::uint64_t us)
{
    collector().onFrameProcessed(us);
    collector().maybeFlush();
}

void onPatternFound(bool found)
{
    collector().onPattern(found);
}

void onPreprocessUs(std::uint64_t us)
{
    collector().onPreprocessDone(us);
}
#else
void init() {}
void shutdown() {}
void onFrameArrival() {}
void onFrameProcessedUs(std::uint64_t) {}
void onPatternFound(bool) {}
void onPreprocessUs(std::uint64_t) {}
#endif
} // namespace collectda
