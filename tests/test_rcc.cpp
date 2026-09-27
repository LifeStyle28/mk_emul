#include "periph/Rcc.h"

#include <gtest/gtest.h>

namespace {

constexpr uint32_t kCr = mcu::kRccBase + 0x00;
constexpr uint32_t kPllCfgr = mcu::kRccBase + 0x04;
constexpr uint32_t kCfgr = mcu::kRccBase + 0x08;
constexpr uint32_t kApb1Enr = mcu::kRccBase + 0x40;
constexpr uint32_t kAhb1Enr = mcu::kRccBase + 0x30;

TEST(Rcc, ResetRunsFromHsi16MHz) {
    Rcc rcc;
    rcc.reset();
    EXPECT_EQ(rcc.sysclk(), 16000000u);
    EXPECT_EQ(rcc.hclk(), 16000000u);
    EXPECT_EQ(rcc.pclk1(), 16000000u);
    EXPECT_EQ(rcc.pclk2(), 16000000u);
    EXPECT_TRUE(rcc.read32(kCr) & 0x2u) << "HSIRDY must be set after reset";
}

// Typical Discovery clock tree: HSE 8 MHz -> PLL 168 MHz, APB1 /4, APB2 /2.
TEST(Rcc, Pll168MHzClockTree) {
    Rcc rcc;
    rcc.reset();
    const uint32_t m = 8, n = 336, p = 0 /* /2 */, q = 7;
    rcc.write32(kPllCfgr, m | (n << 6) | (p << 16) | (1u << 22) /* HSE */ | (q << 24));
    rcc.write32(kCfgr, 2u /* SW=PLL */ | (5u << 10) /* PPRE1 /4 */ | (4u << 13) /* PPRE2 /2 */);

    EXPECT_EQ((rcc.read32(kCfgr) >> 2) & 3u, 2u) << "SWS must follow SW";
    EXPECT_EQ(rcc.sysclk(), 168000000u);
    EXPECT_EQ(rcc.hclk(), 168000000u);
    EXPECT_EQ(rcc.pclk1(), 42000000u);
    EXPECT_EQ(rcc.pclk2(), 84000000u);
    // Timer clocks are doubled when the APB prescaler is not 1.
    EXPECT_EQ(rcc.timclk1(), 84000000u);
    EXPECT_EQ(rcc.timclk2(), 168000000u);
}

TEST(Rcc, AhbPrescaler) {
    Rcc rcc;
    rcc.reset();
    rcc.write32(kCfgr, 0x9u << 4); // HPRE = /4
    EXPECT_EQ(rcc.hclk(), 4000000u);
}

TEST(Rcc, PeripheralClockEnableBits) {
    Rcc rcc;
    rcc.reset();
    EXPECT_FALSE(rcc.gpioEnabled(3));
    EXPECT_FALSE(rcc.usartEnabled(2));
    rcc.write32(kAhb1Enr, 1u << 3);  // GPIODEN
    rcc.write32(kApb1Enr, 1u << 17); // USART2EN
    EXPECT_TRUE(rcc.gpioEnabled(3));
    EXPECT_TRUE(rcc.usartEnabled(2));
    EXPECT_FALSE(rcc.timEnabled(2));
}

} // namespace
