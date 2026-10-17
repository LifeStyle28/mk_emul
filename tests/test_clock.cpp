// Clock tree of the Clock Configuration dialog (CubeMX-like): frequencies, datasheet limits,
// RCC register values and the SystemClock_Config() replacement in project sources.
#include "core/ClockConfig.h"

#include <gtest/gtest.h>

namespace {

constexpr uint32_t MHz = 1000000u;

bool has(const std::vector<std::string> &v, const std::string &s) {
    for (const auto &x : v) {
        if (x == s)
            return true;
    }
    return false;
}

TEST(ClockConfig, DefaultIs168MHzFromHse) {
    const ClockConfig c;
    const auto r = c.compute();
    EXPECT_TRUE(r.ok()) << (r.errors.empty() ? "" : r.errors[0]);
    EXPECT_TRUE(r.warnings.empty());
    EXPECT_EQ(r.vcoIn, 2 * MHz);
    EXPECT_EQ(r.vcoOut, 336 * MHz);
    EXPECT_EQ(r.sysclk, 168 * MHz);
    EXPECT_EQ(r.hclk, 168 * MHz);
    EXPECT_EQ(r.pclk1, 42 * MHz);
    EXPECT_EQ(r.tim1, 84 * MHz) << "APB1 prescaler /4: timers get x2";
    EXPECT_EQ(r.pclk2, 84 * MHz);
    EXPECT_EQ(r.tim2, 168 * MHz);
    EXPECT_EQ(r.pll48, 48 * MHz);
    EXPECT_EQ(r.flashLatency, 5);
}

TEST(ClockConfig, ReportsLimits) {
    ClockConfig c;
    c.pllN = 200; // VCO 400 MHz, PLLCLK 200 MHz
    auto r = c.compute();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(has(r.bad, "pllClk"));
    EXPECT_TRUE(has(r.bad, "sysclk"));
    EXPECT_TRUE(has(r.bad, "pll48")) << "400 / 7 = 57 MHz";

    c = ClockConfig();
    c.apb1Div = 2; // 84 MHz on APB1
    r = c.compute();
    EXPECT_TRUE(has(r.bad, "pclk1"));
    EXPECT_FALSE(has(r.bad, "pclk2"));

    c = ClockConfig();
    c.pllM = 2; // 4 MHz VCO input
    c.pllN = 84;
    r = c.compute();
    EXPECT_TRUE(has(r.bad, "vcoIn"));

    c = ClockConfig();
    c.pllM = 5; // 1.6 MHz: allowed, 2 MHz recommended; 48 MHz clock off
    c.pllN = 210;
    r = c.compute();
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.sysclk, 168 * MHz);
    EXPECT_FALSE(r.warnings.empty());
}

TEST(ClockConfig, PllSettingsIgnoredWhenNotUsed) {
    ClockConfig c;
    c.sysclkSource = ClockConfig::Hsi;
    c.pllN = 1; // invalid, but the PLL is off
    c.apb1Div = 1;
    c.apb2Div = 1;
    const auto r = c.compute();
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(r.sysclk, 16 * MHz);
    EXPECT_EQ(r.pclk1, 16 * MHz);
    EXPECT_EQ(r.tim1, 16 * MHz) << "APB prescaler 1: timers = PCLK";
    EXPECT_EQ(r.flashLatency, 0);
}

TEST(ClockConfig, RegistersRoundTrip) {
    ClockConfig c;
    c.pllSource = ClockConfig::Hsi;
    c.pllM = 16;
    c.pllN = 200;
    c.pllP = 4;
    c.pllQ = 5;
    c.ahbDiv = 2;
    c.apb1Div = 2;
    c.apb2Div = 1;
    EXPECT_EQ(c.pllcfgr(), 16u | (200u << 6) | (1u << 16) | (5u << 24));
    const ClockConfig back = ClockConfig::fromRegisters(c.pllcfgr(), c.cfgrPrescalers());
    EXPECT_EQ(back.pllSource, ClockConfig::Hsi);
    EXPECT_EQ(back.sysclkSource, ClockConfig::Pll);
    EXPECT_EQ(back.pllM, 16u);
    EXPECT_EQ(back.pllN, 200u);
    EXPECT_EQ(back.pllP, 4u);
    EXPECT_EQ(back.pllQ, 5u);
    EXPECT_EQ(back.ahbDiv, 2u);
    EXPECT_EQ(back.apb1Div, 2u);
    EXPECT_EQ(back.apb2Div, 1u);
    // Reset values: HSI, PLLCFGR 0x24003010.
    const ClockConfig reset = ClockConfig::fromRegisters(0x24003010u, 0);
    EXPECT_EQ(reset.compute().sysclk, 16 * MHz);
    EXPECT_EQ(reset.pllM, 16u);
    EXPECT_EQ(reset.pllN, 192u);
}

TEST(ClockConfig, GeneratesCode) {
    const ClockConfig c;
    const std::string hal = c.halCode();
    EXPECT_NE(hal.find("osc.PLL.PLLN = 168;"), std::string::npos) << hal;
    EXPECT_NE(hal.find("RCC_HCLK_DIV4"), std::string::npos);
    EXPECT_NE(hal.find("FLASH_LATENCY_5"), std::string::npos);
    EXPECT_NE(hal.find("SYSCLK  168 MHz"), std::string::npos);
    const std::string reg = c.registerCode();
    EXPECT_NE(reg.find("RCC->PLLCFGR = 4u"), std::string::npos) << reg;
    EXPECT_NE(reg.find("SystemCoreClockUpdate();"), std::string::npos);
}

TEST(ClockConfig, ReplacesFunctionInSource) {
    const std::string src = "#include \"main.h\"\n"
                            "static void SystemClock_Config(void);\n"
                            "int main(void) { SystemClock_Config(); for (;;) {} }\n"
                            "static void SystemClock_Config(void) {\n"
                            "    const char *s = \"}\"; // }\n"
                            "    /* { */ if (1) { (void)s; }\n"
                            "}\n"
                            "void after(void) {}\n";
    const ClockConfig c;
    std::string out;
    ASSERT_TRUE(ClockConfig::replaceFunction(src, c.halCode(), &out));
    EXPECT_NE(out.find("static void SystemClock_Config(void);\n"), std::string::npos) << "prototype kept";
    EXPECT_NE(out.find("static void SystemClock_Config(void)\n{"), std::string::npos) << out;
    EXPECT_EQ(out.find("const char *s"), std::string::npos) << "old body removed";
    EXPECT_NE(out.find("void after(void) {}"), std::string::npos) << out;

    // Applying again replaces the generated comment as well, nothing piles up.
    std::string again;
    ASSERT_TRUE(ClockConfig::replaceFunction(out, c.halCode(), &again));
    EXPECT_EQ(again, out);

    EXPECT_FALSE(ClockConfig::replaceFunction("int main(void) { return 0; }\n", c.halCode(), &out));
}

} // namespace
