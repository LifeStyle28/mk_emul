#pragma once

#include <cstdint>

// Keeps emulation in step with wall-clock time.
//
// The CPU executes instructions in chunks as fast as the host allows. After each chunk the
// caller reports how many cycles ran and at which clock (HCLK can change at run time, e.g. when
// firmware switches to the PLL); the pacer converts that to emulated nanoseconds and returns how
// long to wait so emulated time does not run ahead of real time * speed.
//
// Assumption shared with the timers: 1 instruction = 1 HCLK cycle.
class RealTimePacer {
public:
    // 1.0 = real time, 0.1 = ten times slower, 10 = ten times faster, 0 = unlimited.
    void setSpeed(double speed);
    double speed() const { return speed_; }
    bool unlimited() const { return speed_ <= 0.0; }

    // Call when the CPU starts running (after a pause); nowNs is a monotonic clock.
    void start(uint64_t nowNs);

    // Accounts executed cycles; returns nanoseconds to wait before running the next chunk
    // (0 = continue immediately). If the host cannot keep up, the lag is forgiven instead of
    // being caught up later in a burst.
    uint64_t advance(uint64_t cycles, uint32_t hz, uint64_t nowNs);

    // Instructions to run per chunk so one chunk is about `chunkNs` of real time.
    uint32_t chunkInstructions(uint32_t hz, uint64_t chunkNs = 2000000) const;

    // Measured emulated/real time ratio over the last ~0.5 s (1.0 = real time).
    double measuredSpeed() const { return measured_; }
    // Total emulated time since start() (for tests and diagnostics).
    uint64_t emulatedNs() const { return emuNs_; }

    static constexpr uint64_t kMaxLagNs = 200000000; // 200 ms

private:
    double speed_ = 1.0;
    uint64_t startNs_ = 0;   // real time corresponding to emulated time 0 (shifted on lag)
    uint64_t emuNs_ = 0;     // emulated time since start()
    uint64_t fracCycles_ = 0; // remainder of cycles*1e9/hz, keeps long runs exact
    uint64_t windowRealNs_ = 0;
    uint64_t windowEmuNs_ = 0;
    double measured_ = 0.0;
};
