#include "core/ClockConfig.h"

#include <cstdarg>
#include <cstdio>
#include <regex>

namespace {

constexpr uint32_t kMHz = 1000000u;
constexpr uint32_t kAhbDivs[] = {1, 2, 4, 8, 16, 64, 128, 256, 512};

uint32_t hpreBits(uint32_t div) {
    // HPRE: 0xxx = /1, 1000 = /2 ... 1111 = /512 (no /32)
    for (uint32_t i = 1; i < sizeof(kAhbDivs) / sizeof(kAhbDivs[0]); ++i) {
        if (kAhbDivs[i] == div)
            return 7u + i;
    }
    return 0;
}

uint32_t ppreBits(uint32_t div) {
    // PPRE: 0xx = /1, 100 = /2, 101 = /4, 110 = /8, 111 = /16
    switch (div) {
    case 2: return 4;
    case 4: return 5;
    case 8: return 6;
    case 16: return 7;
    default: return 0;
    }
}

std::string fmt(const char *f, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

} // namespace

std::string ClockConfig::mhz(uint32_t hz) {
    if (hz % kMHz == 0)
        return std::to_string(hz / kMHz) + " MHz";
    std::string s = fmt("%.3f", hz / 1e6);
    while (!s.empty() && s.back() == '0')
        s.pop_back();
    return s + " MHz";
}

bool ClockConfig::validAhb(uint32_t d) {
    for (uint32_t v : kAhbDivs) {
        if (v == d)
            return true;
    }
    return false;
}

bool ClockConfig::validApb(uint32_t d) {
    return d == 1 || d == 2 || d == 4 || d == 8 || d == 16;
}

ClockConfig::Result ClockConfig::compute() const {
    Result r;
    auto error = [&](const std::string &what, const char *field) {
        r.errors.push_back(what);
        if (field)
            r.bad.push_back(field);
    };
    r.pllUsed = sysclkSource == Pll;
    r.pllIn = pllSource == Hse ? kHseHz : kHsiHz;
    if (pllM >= 2 && pllM <= 63) {
        r.vcoIn = r.pllIn / pllM;
        r.vcoOut = static_cast<uint32_t>(static_cast<uint64_t>(r.pllIn) * pllN / pllM);
    }
    if (pllP == 2 || pllP == 4 || pllP == 6 || pllP == 8)
        r.pllClk = r.vcoOut / pllP;
    if (pllQ >= 2 && pllQ <= 15)
        r.pll48 = r.vcoOut / pllQ;

    if (r.pllUsed) {
        if (pllM < 2 || pllM > 63)
            error("PLLM must be 2..63", "pllM");
        else if (r.vcoIn < 1 * kMHz || r.vcoIn > 2 * kMHz)
            error("VCO input " + mhz(r.vcoIn) + ": must be 1..2 MHz (change /M)", "vcoIn");
        else if (r.vcoIn != 2 * kMHz)
            r.warnings.push_back("VCO input " + mhz(r.vcoIn) + ": 2 MHz is recommended (less PLL jitter)");
        if (pllN < 50 || pllN > 432)
            error("PLLN must be 50..432", "pllN");
        else if (r.vcoOut < 100 * kMHz || r.vcoOut > 432 * kMHz)
            error("VCO output " + mhz(r.vcoOut) + ": must be 100..432 MHz", "vcoOut");
        if (r.pllClk == 0)
            error("PLLP must be 2, 4, 6 or 8", "pllP");
        else if (r.pllClk > 168 * kMHz)
            error("PLLCLK " + mhz(r.pllClk) + " exceeds 168 MHz", "pllClk");
        if (pllQ < 2 || pllQ > 15)
            error("PLLQ must be 2..15", "pllQ");
        else if (r.pll48 > 48 * kMHz)
            error("48 MHz clock is " + mhz(r.pll48) + ": exceeds 48 MHz", "pll48");
        else if (r.pll48 != 48 * kMHz)
            r.warnings.push_back("48 MHz clock is " + mhz(r.pll48) + ": USB OTG FS needs exactly 48 MHz");
    }

    r.sysclk = sysclkSource == Hsi ? kHsiHz : sysclkSource == Hse ? kHseHz : r.pllClk;
    if (r.sysclk > 168 * kMHz)
        error("SYSCLK " + mhz(r.sysclk) + " exceeds 168 MHz", "sysclk");
    if (!validAhb(ahbDiv))
        error("AHB prescaler must be 1, 2, 4, 8, 16, 64, 128, 256 or 512", "ahbDiv");
    else
        r.hclk = r.sysclk / ahbDiv;
    if (r.hclk > 168 * kMHz)
        error("HCLK " + mhz(r.hclk) + " exceeds 168 MHz", "hclk");
    if (!validApb(apb1Div) || !validApb(apb2Div)) {
        error("APB prescalers must be 1, 2, 4, 8 or 16", nullptr);
    } else {
        r.pclk1 = r.hclk / apb1Div;
        r.pclk2 = r.hclk / apb2Div;
        r.tim1 = apb1Div == 1 ? r.pclk1 : r.pclk1 * 2;
        r.tim2 = apb2Div == 1 ? r.pclk2 : r.pclk2 * 2;
    }
    if (r.pclk1 > 42 * kMHz)
        error("APB1 (PCLK1) " + mhz(r.pclk1) + " exceeds 42 MHz (raise the APB1 prescaler)", "pclk1");
    if (r.pclk2 > 84 * kMHz)
        error("APB2 (PCLK2) " + mhz(r.pclk2) + " exceeds 84 MHz (raise the APB2 prescaler)", "pclk2");
    // RM0090 table 10, 2.7-3.6 V: one wait state per 30 MHz.
    r.flashLatency = r.hclk == 0 ? 0 : static_cast<int>((r.hclk - 1) / (30 * kMHz));
    if (r.flashLatency > 7)
        r.flashLatency = 7;
    return r;
}

ClockConfig ClockConfig::fromRegisters(uint32_t pllcfgr, uint32_t cfgr) {
    ClockConfig c;
    const uint32_t sws = (cfgr >> 2) & 3u;
    c.sysclkSource = sws == 1 ? Hse : sws == 2 ? Pll : Hsi;
    c.pllSource = (pllcfgr & (1u << 22)) ? Hse : Hsi;
    c.pllM = pllcfgr & 0x3Fu;
    c.pllN = (pllcfgr >> 6) & 0x1FFu;
    c.pllP = (((pllcfgr >> 16) & 3u) + 1u) * 2u;
    c.pllQ = (pllcfgr >> 24) & 0xFu;
    const uint32_t hpre = (cfgr >> 4) & 0xFu;
    c.ahbDiv = hpre < 8 ? 1 : kAhbDivs[hpre - 7];
    const uint32_t p1 = (cfgr >> 10) & 7u;
    const uint32_t p2 = (cfgr >> 13) & 7u;
    c.apb1Div = p1 < 4 ? 1 : 1u << (p1 - 3);
    c.apb2Div = p2 < 4 ? 1 : 1u << (p2 - 3);
    return c;
}

uint32_t ClockConfig::pllcfgr() const {
    return (pllM & 0x3Fu) | ((pllN & 0x1FFu) << 6) | (((pllP / 2 - 1) & 3u) << 16) |
           (pllSource == Hse ? 1u << 22 : 0u) | ((pllQ & 0xFu) << 24);
}

uint32_t ClockConfig::cfgrPrescalers() const {
    const uint32_t sw = static_cast<uint32_t>(sysclkSource);
    return sw | (sw << 2) | (hpreBits(ahbDiv) << 4) | (ppreBits(apb1Div) << 10) | (ppreBits(apb2Div) << 13);
}

std::string ClockConfig::summary() const {
    const Result r = compute();
    return "SYSCLK " + mhz(r.sysclk) + ", HCLK " + mhz(r.hclk) + ", APB1 " + mhz(r.pclk1) + " (timers " +
           mhz(r.tim1) + "), APB2 " + mhz(r.pclk2) + " (timers " + mhz(r.tim2) + ")";
}

namespace {

std::string header(const ClockConfig &c, const ClockConfig::Result &r) {
    const char *src[] = {"HSI 16 MHz", "HSE 8 MHz", "PLL"};
    std::string s = "/* Clock configuration generated by mk_emul (Build > Clock Configuration), as in\n"
                    " * the Clock Configuration tab of STM32CubeMX.\n";
    if (r.pllUsed)
        s += fmt(" *   SYSCLK  %-9s <- PLL: %s / M %u * N %u / P %u\n", ClockConfig::mhz(r.sysclk).c_str(),
                 c.pllSource == ClockConfig::Hse ? "HSE 8 MHz" : "HSI 16 MHz", c.pllM, c.pllN, c.pllP);
    else
        s += fmt(" *   SYSCLK  %-9s <- %s\n", ClockConfig::mhz(r.sysclk).c_str(), src[c.sysclkSource]);
    s += fmt(" *   HCLK    %-9s (AHB /%u), flash latency %d wait states\n", ClockConfig::mhz(r.hclk).c_str(),
             c.ahbDiv, r.flashLatency);
    s += fmt(" *   APB1    %-9s (/%u), APB1 timers %s\n", ClockConfig::mhz(r.pclk1).c_str(), c.apb1Div,
             ClockConfig::mhz(r.tim1).c_str());
    s += fmt(" *   APB2    %-9s (/%u), APB2 timers %s\n", ClockConfig::mhz(r.pclk2).c_str(), c.apb2Div,
             ClockConfig::mhz(r.tim2).c_str());
    if (r.pllUsed)
        s += fmt(" *   48 MHz clock (USB, SDIO, RNG) %s (/Q %u)\n", ClockConfig::mhz(r.pll48).c_str(), c.pllQ);
    s += " */\n";
    return s;
}

} // namespace

std::string ClockConfig::halCode() const {
    const Result r = compute();
    const bool hse = sysclkSource == Hse || (r.pllUsed && pllSource == Hse);
    std::string s = header(*this, r);
    s += "void SystemClock_Config(void)\n{\n"
         "    RCC_OscInitTypeDef osc = {0};\n"
         "    RCC_ClkInitTypeDef clk = {0};\n\n"
         "    /* Voltage scale 1: needed for HCLK above 144 MHz. */\n"
         "    __HAL_RCC_PWR_CLK_ENABLE();\n"
         "    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);\n\n";
    if (hse) {
        s += "    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;\n"
             "    osc.HSEState = RCC_HSE_ON;\n";
    } else {
        s += "    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;\n"
             "    osc.HSIState = RCC_HSI_ON;\n"
             "    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;\n";
    }
    if (r.pllUsed) {
        s += "    osc.PLL.PLLState = RCC_PLL_ON;\n";
        s += pllSource == Hse ? "    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;\n" : "    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;\n";
        s += fmt("    osc.PLL.PLLM = %u;\n    osc.PLL.PLLN = %u;\n    osc.PLL.PLLP = RCC_PLLP_DIV%u;\n"
                 "    osc.PLL.PLLQ = %u;\n",
                 pllM, pllN, pllP, pllQ);
    } else {
        s += "    osc.PLL.PLLState = RCC_PLL_NONE;\n";
    }
    const char *sw = sysclkSource == Pll ? "RCC_SYSCLKSOURCE_PLLCLK" : sysclkSource == Hse ? "RCC_SYSCLKSOURCE_HSE"
                                                                                       : "RCC_SYSCLKSOURCE_HSI";
    s += "    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {\n        for (;;) {\n        }\n    }\n\n"
         "    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;\n";
    s += fmt("    clk.SYSCLKSource = %s;\n    clk.AHBCLKDivider = RCC_SYSCLK_DIV%u;\n"
             "    clk.APB1CLKDivider = RCC_HCLK_DIV%u;\n    clk.APB2CLKDivider = RCC_HCLK_DIV%u;\n",
             sw, ahbDiv, apb1Div, apb2Div);
    s += fmt("    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_%d) != HAL_OK) {\n        for (;;) {\n        }\n    }\n}\n",
             r.flashLatency);
    return s;
}

std::string ClockConfig::registerCode() const {
    const Result r = compute();
    const bool hse = sysclkSource == Hse || (r.pllUsed && pllSource == Hse);
    const uint32_t sw = static_cast<uint32_t>(sysclkSource);
    std::string s = header(*this, r);
    s += "void SystemClock_Config(void)\n{\n"
         "    /* Voltage scale 1 (PWR_CR.VOS): needed for HCLK above 144 MHz. */\n"
         "    RCC->APB1ENR |= 1u << 28; /* PWREN */\n"
         "    PWR->CR |= 1u << 14;\n\n"
         "    /* Run from HSI while the clock tree changes. */\n"
         "    RCC->CR |= 1u << 0; /* HSION */\n"
         "    while (!(RCC->CR & (1u << 1))) {\n    }\n"
         "    RCC->CFGR &= ~3u; /* SW = HSI */\n"
         "    while ((RCC->CFGR & (3u << 2)) != 0u) {\n    }\n";
    if (hse)
        s += "\n    RCC->CR |= 1u << 16; /* HSEON */\n"
             "    while (!(RCC->CR & (1u << 17))) { /* HSERDY */\n    }\n";
    if (r.pllUsed) {
        s += "\n    RCC->CR &= ~(1u << 24); /* PLLON = 0 before changing PLLCFGR */\n"
             "    while (RCC->CR & (1u << 25)) {\n    }\n";
        s += fmt("    RCC->PLLCFGR = %uu               /* PLLM */\n"
                 "                 | (%uu << 6)        /* PLLN */\n"
                 "                 | (%uu << 16)       /* PLLP = /%u */\n"
                 "                 | (%uu << 22)       /* PLLSRC = %s */\n"
                 "                 | (%uu << 24);      /* PLLQ */\n",
                 pllM, pllN, pllP / 2 - 1, pllP, pllSource == Hse ? 1u : 0u, pllSource == Hse ? "HSE" : "HSI", pllQ);
        s += "    RCC->CR |= 1u << 24; /* PLLON */\n"
             "    while (!(RCC->CR & (1u << 25))) { /* PLLRDY */\n    }\n";
    }
    s += fmt("\n    /* More flash wait states before the clock gets faster. */\n"
             "    if ((FLASH->ACR & 7u) < %du)\n"
             "        FLASH->ACR = (FLASH->ACR & ~7u) | %du;\n\n",
             r.flashLatency, r.flashLatency);
    s += fmt("    /* AHB /%u, APB1 /%u, APB2 /%u, then SYSCLK = %s. */\n"
             "    RCC->CFGR = (RCC->CFGR & ~0xFCF0u) | (%uu << 4) | (%uu << 10) | (%uu << 13);\n"
             "    RCC->CFGR = (RCC->CFGR & ~3u) | %uu;\n"
             "    while (((RCC->CFGR >> 2) & 3u) != %uu) {\n    }\n\n",
             ahbDiv, apb1Div, apb2Div, sysclkSource == Pll ? "PLLCLK" : sysclkSource == Hse ? "HSE" : "HSI",
             hpreBits(ahbDiv), ppreBits(apb1Div), ppreBits(apb2Div), sw, sw);
    s += fmt("    /* Prefetch, instruction and data caches, %d wait states. */\n"
             "    FLASH->ACR = (1u << 8) | (1u << 9) | (1u << 10) | %du;\n\n"
             "    SystemCoreClockUpdate(); /* SystemCoreClock = HCLK */\n}\n",
             r.flashLatency, r.flashLatency);
    return s;
}

bool ClockConfig::replaceFunction(const std::string &source, const std::string &code, std::string *out) {
    static const std::regex def(R"((static\s+)?void\s+SystemClock_Config\s*\(\s*void\s*\)\s*\{)");
    std::smatch m;
    if (!std::regex_search(source, m, def))
        return false;
    size_t begin = static_cast<size_t>(m.position(0));
    const bool isStatic = m[1].matched;
    // Body: match braces, ignoring the ones in comments, strings and character literals.
    size_t i = begin + static_cast<size_t>(m.length(0));
    int depth = 1;
    while (i < source.size() && depth > 0) {
        const char c = source[i];
        if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            i = source.find('\n', i);
            if (i == std::string::npos)
                return false;
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            i = source.find("*/", i + 2);
            if (i == std::string::npos)
                return false;
            i += 2;
            continue;
        } else if (c == '"' || c == '\'') {
            for (++i; i < source.size() && source[i] != c; ++i) {
                if (source[i] == '\\')
                    ++i;
            }
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
        }
        ++i;
    }
    if (depth != 0)
        return false;
    size_t end = i; // after the closing brace
    if (end < source.size() && source[end] == '\n')
        ++end;
    // A header comment from an earlier "Write to Project" right before the function goes too.
    const std::string marker = "/* Clock configuration generated by mk_emul";
    const size_t prevEnd = source.rfind("*/", begin);
    if (prevEnd != std::string::npos && source.find_first_not_of(" \t\r\n", prevEnd + 2) == begin) {
        const size_t prevStart = source.rfind("/*", prevEnd);
        if (prevStart != std::string::npos && source.compare(prevStart, marker.size(), marker) == 0)
            begin = prevStart;
    }
    std::string replacement = code;
    if (isStatic) {
        const size_t at = replacement.find("void SystemClock_Config(void)");
        if (at != std::string::npos)
            replacement.insert(at, "static ");
    }
    *out = source.substr(0, begin) + replacement + source.substr(end);
    return true;
}
