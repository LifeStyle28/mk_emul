#include "core/RealTimePacer.h"

#include <gtest/gtest.h>

namespace {

constexpr uint64_t kMs = 1000000;

TEST(RealTimePacer, WaitsWhenAheadOfRealTime) {
    RealTimePacer p;
    p.start(0);
    // 16000 cycles at 16 MHz = 1 ms emulated, done in 0.1 ms real -> wait 0.9 ms.
    EXPECT_EQ(p.advance(16000, 16000000, kMs / 10), 9 * kMs / 10);
    EXPECT_EQ(p.emulatedNs(), kMs);
}

TEST(RealTimePacer, NoWaitWhenBehind) {
    RealTimePacer p;
    p.start(0);
    EXPECT_EQ(p.advance(16000, 16000000, 5 * kMs), 0u);
}

TEST(RealTimePacer, FollowsClockChanges) {
    RealTimePacer p;
    p.start(0);
    p.advance(16000, 16000000, 0);    // 1 ms at HSI 16 MHz
    p.advance(168000, 168000000, 0);  // 1 ms at PLL 168 MHz
    EXPECT_EQ(p.emulatedNs(), 2 * kMs);
}

TEST(RealTimePacer, SpeedFactor) {
    RealTimePacer p;
    p.setSpeed(0.5); // half speed: 1 ms emulated takes 2 ms
    p.start(0);
    EXPECT_EQ(p.advance(16000, 16000000, 0), 2 * kMs);
    p.setSpeed(10.0);
    p.start(0);
    EXPECT_EQ(p.advance(160000, 16000000, 0), 1 * kMs); // 10 ms emulated in 1 ms
}

TEST(RealTimePacer, UnlimitedNeverWaits) {
    RealTimePacer p;
    p.setSpeed(0);
    p.start(0);
    EXPECT_TRUE(p.unlimited());
    EXPECT_EQ(p.advance(100000000, 16000000, 0), 0u);
}

TEST(RealTimePacer, ExactOverManySmallChunks) {
    RealTimePacer p;
    p.start(0);
    // 1 cycle at 3 Hz = 333333333.3 ns: the remainder must not be lost.
    for (int i = 0; i < 3000; ++i)
        p.advance(1, 3, 0);
    EXPECT_EQ(p.emulatedNs(), 1000u * 1000000000ull);
}

TEST(RealTimePacer, ForgivesLagInsteadOfBursting) {
    RealTimePacer p;
    p.start(0);
    // Host stalled for 1 s: emulated only 1 ms.
    EXPECT_EQ(p.advance(16000, 16000000, 1000 * kMs), 0u);
    // Next 1 ms emulated right away must wait ~1 ms, not run 999 ms flat out.
    EXPECT_EQ(p.advance(16000, 16000000, 1000 * kMs), 1 * kMs);
}

TEST(RealTimePacer, MeasuresSpeed) {
    RealTimePacer p;
    p.start(0);
    p.advance(8000000, 16000000, 500 * kMs); // 0.5 s emulated in 0.5 s real
    EXPECT_NEAR(p.measuredSpeed(), 1.0, 1e-9);
}

TEST(RealTimePacer, ChunkIsAboutTwoMilliseconds) {
    RealTimePacer p;
    EXPECT_EQ(p.chunkInstructions(16000000), 32000u);
    EXPECT_EQ(p.chunkInstructions(168000000), 50000u); // clamped
    p.setSpeed(0.001);
    EXPECT_EQ(p.chunkInstructions(16000000), 64u); // clamped, stays responsive
}

} // namespace
