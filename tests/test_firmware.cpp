// Runs the real examples/* firmware (built by CMake with arm-none-eabi-gcc) on the Machine.
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <string>

namespace {

#ifdef MK_EMUL_FW_DIR
const std::string kFwDir = MK_EMUL_FW_DIR;
#endif

struct FirmwareTest : ::testing::Test {
    Machine m;
    std::string uart;

    void SetUp() override {
#ifndef MK_EMUL_FW_DIR
        GTEST_SKIP() << "arm-none-eabi-gcc not found at configure time";
#else
        std::string err;
        ASSERT_TRUE(m.initialize(&err)) << err;
        m.setUartTx([this](uint8_t b) { uart.push_back(static_cast<char>(b)); });
#endif
    }

    void load(const std::string &name) {
#ifdef MK_EMUL_FW_DIR
        const auto img = m.loadFirmware(kFwDir + "/" + name + ".elf");
        ASSERT_TRUE(img.ok) << img.error;
#endif
    }

    // Number of changes of the LED bits in `mask` while running `insns` instructions.
    int countLedToggles(uint64_t insns, uint32_t mask, CpuEngine::StopReason *stop = nullptr) {
        uint32_t last = m.snapshot().leds & mask;
        int toggles = 0;
        auto s = runSampling(m, insns, [&] {
            const uint32_t now = m.snapshot().leds & mask;
            if (now != last)
                ++toggles;
            last = now;
        });
        if (stop)
            *stop = s;
        return toggles;
    }
};

TEST_F(FirmwareTest, BlinkyTogglesLeds) {
    load("blinky");
    CpuEngine::StopReason stop;
    EXPECT_GE(countLedToggles(3000000, 0xF, &stop), 2);
    EXPECT_NE(stop, CpuEngine::StopReason::Fault) << m.status();
}

TEST_F(FirmwareTest, UartPrintfWritesToUsart2) {
    load("uart_printf");
    runFor(m, 5000000);
    EXPECT_EQ(uart.rfind("STM32F407 emulator UART2\r\n", 0), 0u) << "got: " << uart;
    EXPECT_NE(uart.find("tick\r\n"), std::string::npos);
}

// SysTick 1 ms delay, green LED toggles every 200 ms = 3.2M cycles at 16 MHz.
TEST_F(FirmwareTest, SysTickExtiBlinksGreenLed) {
    load("systick_exti");
    CpuEngine::StopReason stop;
    EXPECT_GE(countLedToggles(8000000, 0x1, &stop), 2);
    EXPECT_NE(stop, CpuEngine::StopReason::Fault) << m.status();
}

// USER button (PA0) -> EXTI0 rising edge -> handler toggles the red LED (PD14).
TEST_F(FirmwareTest, SysTickExtiButtonTogglesRedLed) {
    load("systick_exti");
    runFor(m, 200000);
    const bool red = m.snapshot().leds & 0x4;

    m.setButtonPressed(true);
    runFor(m, 20000);
    EXPECT_NE(static_cast<bool>(m.snapshot().leds & 0x4), red) << "press must toggle PD14";

    m.setButtonPressed(false); // falling edge: no trigger configured
    runFor(m, 20000);
    EXPECT_NE(static_cast<bool>(m.snapshot().leds & 0x4), red);

    m.setButtonPressed(true);
    runFor(m, 20000);
    EXPECT_EQ(static_cast<bool>(m.snapshot().leds & 0x4), red) << "second press toggles back";
    EXPECT_NE(m.cpu().lastStop(), CpuEngine::StopReason::Fault) << m.status();
}

// Regression (found by the manual UI checklist): loading a firmware over one that already ran,
// as the UI does on Build & Load, must behave like a fresh start.
TEST_F(FirmwareTest, LoadingOverPreviousFirmwareStartsClean) {
    load("uart_printf");
    runFor(m, 1000000);
    load("systick_exti");
    CpuEngine::StopReason stop;
    EXPECT_GE(countLedToggles(8000000, 0x1, &stop), 2);
    EXPECT_NE(stop, CpuEngine::StopReason::Fault) << m.status();
}

TEST_F(FirmwareTest, ReloadingSameInterruptFirmwareStartsClean) {
    load("systick_exti");
    runFor(m, 5000000); // likely stopped inside SysTick_Handler at some point
    load("systick_exti");
    CpuEngine::StopReason stop;
    EXPECT_GE(countLedToggles(8000000, 0x1, &stop), 2);
    EXPECT_NE(stop, CpuEngine::StopReason::Fault) << m.status();
}

TEST_F(FirmwareTest, HardFaultExampleReachesHandler) {
    load("hardfault");
    ASSERT_EQ(runFor(m, 2000000), CpuEngine::StopReason::Fault);
    EXPECT_EQ(m.status(), "HardFault: read unmapped @ 0x60000000");
    const auto s = m.snapshot();
    EXPECT_EQ(s.cfsr, 0x00008200u);
    EXPECT_EQ(s.hfsr, 0x40000000u);
    EXPECT_EQ(s.bfar, 0x60000000u);

    runFor(m, 10000); // HardFault_Handler turns the red LED on and spins
    EXPECT_TRUE(m.snapshot().leds & 0x4);
}

// Emulation speed at Max (no pacing), in millions of instructions per second. Not a check, so
// disabled by default; run with:
//   build\tests\mk_emul_tests --gtest_also_run_disabled_tests --gtest_filter=*Speed*
TEST_F(FirmwareTest, DISABLED_Speed) {
    for (const char *name : {"blinky", "uart_printf", "systick_exti"}) {
        load(name);
        constexpr uint64_t kInsns = 50000000;
        const auto t0 = std::chrono::steady_clock::now();
        runFor(m, kInsns);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%-14s %6.1f MIPS\n", name, kInsns / sec / 1e6);
    }
}

} // namespace
