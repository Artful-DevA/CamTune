// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the complete threaded engine against the synthetic test-pattern source
// while hammering it with parameter changes, and checks that frame rate,
// latency and memory stay flat. Usage: camcore_soak [seconds] [fps]

#include "core/Clock.h"
#include "pipeline/Engine.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <thread>
#include <unistd.h>

using namespace cam;

static long rssKb()
{
    std::ifstream f("/proc/self/statm");
    long pages = 0, resident = 0;
    f >> pages >> resident;
    return resident * (sysconf(_SC_PAGESIZE) / 1024);
}

int main(int argc, char **argv)
{
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 6;
    const int fps = argc > 2 ? std::atoi(argv[2]) : 60;

    std::atomic<int> previews{0};
    EngineCallbacks cb;
    cb.previewReady = [&] { previews.fetch_add(1); };
    Engine engine(cb);

    CameraSelection sel;
    sel.testPattern = true;
    OutputConfig out;
    out.width = 1920;
    out.height = 1080;
    out.fps = fps;
    out.enabled = false; // no v4l2loopback needed
    engine.setOutput(out);
    engine.selectCamera(sel);
    engine.setPreviewWanted(true);
    engine.start();

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> u(0, 1);
    long rssAfterWarmup = 0;
    double worstLatency = 0, minFps = 1e9;
    int64_t start = monotonicNs();
    int64_t nextStats = start + 1000000000LL;
    uint64_t taken = 0;
    int second = 0;
    int failures = 0;

    while (monotonicNs() - start < int64_t(seconds) * 1000000000LL) {
        // Consume previews like the UI does.
        if (engine.takePreviewFrame())
            ++taken;

        // Constant parameter churn, as when dragging sliders.
        FramingParams f;
        f.zoom = 1 + 2 * u(rng);
        f.panX = u(rng) * 2 - 1;
        f.panY = u(rng) * 2 - 1;
        f.rotation = (u(rng) < 0.1) ? u(rng) * 20 - 10 : 0;
        engine.setFraming(f, 90);
        ColorParams c;
        c.brightness = u(rng) * 0.2 - 0.1;
        c.contrast = 0.8 + 0.4 * u(rng);
        c.saturation = u(rng) * 2;
        c.sharpness = u(rng) < 0.5 ? 0 : u(rng);
        engine.setColor(c);
        if (u(rng) < 0.02) {
            EffectParams e;
            e.mode = EffectMode(int(u(rng) * 4));
            engine.setEffects(e);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));

        if (monotonicNs() >= nextStats) {
            nextStats += 1000000000LL;
            ++second;
            EngineStats s = engine.stats();
            std::printf("t=%2ds capture %5.1f fps  process %5.1f fps  proc %5.2f ms  latency %5.1f ms  "
                        "dropped %llu  rss %ld KiB\n",
                        second, s.captureFps, s.processFps, s.processMs, s.latencyMs,
                        (unsigned long long)s.droppedFrames, rssKb());
            if (second == 2)
                rssAfterWarmup = rssKb();
            if (second >= 2) {
                worstLatency = std::max(worstLatency, s.latencyMs);
                minFps = std::min(minFps, s.processFps);
            }
        }
    }
    engine.stop();
    const long rssEnd = rssKb();
    std::printf("previews consumed: %llu, rss growth after warm-up: %ld KiB, worst latency %.1f ms, "
                "min processed fps %.1f\n",
                (unsigned long long)taken, rssEnd - rssAfterWarmup, worstLatency, minFps);

    // Latency must not accumulate: frames are dropped, never queued.
    if (worstLatency > 1000.0 / fps * 4 + 30) {
        std::printf("FAIL: latency too high\n");
        ++failures;
    }
    if (seconds >= 4 && rssEnd - rssAfterWarmup > 16 * 1024) {
        std::printf("FAIL: memory grew\n");
        ++failures;
    }
    if (seconds >= 4 && minFps < fps * 0.5) {
        std::printf("FAIL: frame rate collapsed\n");
        ++failures;
    }
    return failures ? 1 : 0;
}
