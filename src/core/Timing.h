#pragma once
#include <algorithm>
#include <chrono>

namespace vrtc {
    // Frame clock with a fixed-step accumulator. (File is Timing.h because Time.h would shadow <time.h> on Windows.) The step length is passed in, since core/ cannot see Config.h.
    class Time {
        double step;
        double last;
        double dt = 0;
        double accumulator = 0;

    public:
        explicit Time(double fixedStep) : step(fixedStep), last(Now()) {
        }

        // Call once per frame, before running any updates.
        void Tick() {
            const double current = Now();
            dt = current - last;
            last = current;
            // Clamp to avoid the spiral of death after a long stall (breakpoint, window drag).
            if (dt > 0.25) dt = 0.25;
            accumulator += dt;
        }

        // True once per fixed step owed; use in a while loop.
        bool ShouldStep() {
            if (accumulator >= step) {
                accumulator -= step;
                return true;
            }
            return false;
        }

        [[nodiscard]] double Dt() const { return dt; }

        [[nodiscard]] double Step() const { return step; }

        // Fraction of the way into the next step, for render interpolation.
        [[nodiscard]] double Alpha() const { return accumulator / step; }

        [[nodiscard]] double TimeUntilStep() const { return std::max(0.0, step - accumulator); }

        // Seconds since the first call. Measured from our own start rather than the clock's epoch so the value
        // stays small enough to survive a cast to float.
        static double Now() {
            using clock = std::chrono::steady_clock;
            static const clock::time_point start = clock::now();
            return std::chrono::duration<double>(clock::now() - start).count();
        }
    };
}
