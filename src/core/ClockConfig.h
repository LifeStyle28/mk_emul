#pragma once

#include <cstdint>
#include <string>
#include <vector>

// STM32F407 clock tree, as in the Clock Configuration tab of STM32CubeMX: sources (HSI 16 MHz,
// HSE 8 MHz on the Discovery board), main PLL (/M, xN, /P, /Q), system clock mux, AHB and APB
// prescalers. compute() gives every frequency and checks the datasheet limits; the code
// generators write the matching SystemClock_Config() for a HAL or a register-level project.
struct ClockConfig {
    enum Source { Hsi = 0, Hse = 1, Pll = 2 };

    static constexpr uint32_t kHsiHz = 16000000u;
    static constexpr uint32_t kHseHz = 8000000u; // the emulator's (and Discovery's) crystal

    Source pllSource = Hse; // Hsi or Hse
    Source sysclkSource = Pll;
    uint32_t pllM = 4; // VCO input 2 MHz (recommended; CubeMX often uses 8 = 1 MHz)
    uint32_t pllN = 168;
    uint32_t pllP = 2; // 2, 4, 6, 8
    uint32_t pllQ = 7;
    uint32_t ahbDiv = 1;  // 1, 2, 4, 8, 16, 64, 128, 256, 512
    uint32_t apb1Div = 4; // 1, 2, 4, 8, 16
    uint32_t apb2Div = 2;

    struct Result {
        uint32_t pllIn = 0; // PLL source clock
        uint32_t vcoIn = 0;
        uint32_t vcoOut = 0;
        uint32_t pllClk = 0; // /P output
        uint32_t pll48 = 0;  // /Q output: USB OTG FS, SDIO, RNG
        uint32_t sysclk = 0;
        uint32_t hclk = 0;
        uint32_t pclk1 = 0;
        uint32_t pclk2 = 0;
        uint32_t tim1 = 0; // APB1 timer clock (x2 when the APB1 prescaler is not 1)
        uint32_t tim2 = 0;
        int flashLatency = 0; // wait states for HCLK at 2.7-3.6 V
        bool pllUsed = false;
        std::vector<std::string> errors;   // out of the datasheet limits
        std::vector<std::string> warnings; // works, but not as recommended
        std::vector<std::string> bad;      // names of the values that are out of range
        bool ok() const { return errors.empty(); }
    };
    Result compute() const;

    // Values from RCC registers (what the firmware configured), for "From Emulator".
    static ClockConfig fromRegisters(uint32_t pllcfgr, uint32_t cfgr);
    // The RCC register values this configuration produces (tests compare with the machine).
    uint32_t pllcfgr() const;
    uint32_t cfgrPrescalers() const;

    // void SystemClock_Config(void) { ... } with a comment that lists the frequencies.
    std::string halCode() const;
    std::string registerCode() const;
    std::string summary() const; // "SYSCLK 168 MHz, HCLK 168 MHz, APB1 42 MHz, ..."

    // Replaces the SystemClock_Config() definition in a C file with `code` (from halCode() /
    // registerCode()), as CubeMX regenerates it in main.c. Keeps "static", skips comments and
    // strings when matching braces, replaces an earlier generated header comment too.
    // false: the file has no definition.
    static bool replaceFunction(const std::string &source, const std::string &code, std::string *out);

    static std::string mhz(uint32_t hz);
    static bool validAhb(uint32_t d);
    static bool validApb(uint32_t d);
};
