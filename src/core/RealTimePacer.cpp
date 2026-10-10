#include "core/RealTimePacer.h"

#include <algorithm>

void RealTimePacer::setSpeed(double speed) {
    speed_ = speed > 0.0 ? speed : 0.0;
}

void RealTimePacer::start(uint64_t nowNs) {
    startNs_ = nowNs;
    emuNs_ = 0;
    fracCycles_ = 0;
    windowRealNs_ = nowNs;
    windowEmuNs_ = 0;
    measured_ = 0.0;
}

uint64_t RealTimePacer::advance(uint64_t cycles, uint32_t hz, uint64_t nowNs) {
    if (hz == 0)
        hz = 1;
    // cycles per call are small (one chunk), so cycles * 1e9 cannot overflow 64 bits.
    const uint64_t scaled = cycles * 1000000000ull + fracCycles_;
    emuNs_ += scaled / hz;
    fracCycles_ = scaled % hz;

    if (nowNs - windowRealNs_ >= 500000000ull) {
        measured_ = static_cast<double>(emuNs_ - windowEmuNs_) / static_cast<double>(nowNs - windowRealNs_);
        windowRealNs_ = nowNs;
        windowEmuNs_ = emuNs_;
    }

    if (unlimited())
        return 0;
    const uint64_t targetRealNs = static_cast<uint64_t>(static_cast<double>(emuNs_) / speed_);
    const uint64_t realNs = nowNs - startNs_;
    if (targetRealNs > realNs)
        return targetRealNs - realNs;
    if (realNs - targetRealNs > kMaxLagNs)
        startNs_ = nowNs - targetRealNs; // host too slow (or was busy): drop the debt
    return 0;
}

uint32_t RealTimePacer::chunkInstructions(uint32_t hz, uint64_t chunkNs) const {
    if (unlimited())
        return 12000;
    const double n = static_cast<double>(hz) * speed_ * static_cast<double>(chunkNs) / 1e9;
    return static_cast<uint32_t>(std::clamp(n, 64.0, 50000.0));
}
