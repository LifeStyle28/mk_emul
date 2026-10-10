// Runs the real examples/* firmware (built by CMake with arm-none-eabi-gcc) on the Machine.
#include "TestUtil.h"

#include <gtest/gtest.h>

#include <algorithm>
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
    std::string warnings; // emulator warnings, "; "-separated

    void SetUp() override {
#ifndef MK_EMUL_FW_DIR
        GTEST_SKIP() << "arm-none-eabi-gcc not found at configure time";
#else
        std::string err;
        ASSERT_TRUE(m.initialize(&err)) << err;
        m.setUartTx([this](uint8_t b) { uart.push_back(static_cast<char>(b)); });
        m.setWarningSink([this](const std::string &w) { warnings += w + "; "; });
#endif
    }

    // The examples must only use emulated peripherals: a warning means the example (or HAL /
    // FreeRTOS) relies on something the emulator silently ignores.
    void TearDown() override { EXPECT_EQ(warnings, "") << "unemulated peripheral accessed"; }

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

// Float math in main() and in a SysTick handler firing every 157 cycles: the compiler relies on
// the hardware saving S0-S15/FPSCR, so without the extended frame main() computes a wrong sum.
TEST_F(FirmwareTest, FpuIrqKeepsFloatRegistersOfInterruptedCode) {
    load("fpu_irq");
    CpuEngine::StopReason stop;
    EXPECT_GE(countLedToggles(3000000, 0x1, &stop), 2) << "green: rounds computed correctly";
    EXPECT_NE(stop, CpuEngine::StopReason::Fault) << m.status();
    EXPECT_FALSE(m.snapshot().leds & 0x4) << "red: FP registers were corrupted by the interrupt";
}

// TIM4 CH1-CH4 on PD12-PD15 (AF2): LED brightness = PWM duty; CCR1 ramps from SysTick.
TEST_F(FirmwareTest, Tim4PwmSetsLedBrightness) {
    load("tim4_pwm");
    float minGreen = 1.0f, maxGreen = 0.0f;
    int asleep = 0;
    const uint64_t start = m.cpu().instructionCount();
    auto stop = runSampling(m, 16000000, [&] { // 1 s: green ramps 0 -> 100%
        asleep += m.snapshot().sleeping;
        const float g = m.snapshot().ledLevel[0];
        minGreen = std::min(minGreen, g);
        maxGreen = std::max(maxGreen, g);
    });
    EXPECT_NE(stop, CpuEngine::StopReason::Fault) << m.status();
    const auto s = m.snapshot();
    EXPECT_FLOAT_EQ(s.ledLevel[1], 0.25f);
    EXPECT_FLOAT_EQ(s.ledLevel[2], 0.5f);
    EXPECT_FLOAT_EQ(s.ledLevel[3], 1.0f);
    EXPECT_EQ(s.leds & 0xEu, 0xEu);
    EXPECT_LT(minGreen, 0.05f);
    EXPECT_GT(maxGreen, 0.9f);
    EXPECT_GT(asleep, 0) << "main loop sleeps in WFI between SysTick interrupts";
    EXPECT_GE(m.cpu().instructionCount() - start, 16000000u) << "sleep time counts as cycles";
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

// CubeMX-style project on the real ST HAL: HSE + PLL to 168 MHz (HAL waits for HSERDY, PLLRDY,
// SWS, FLASH latency; enables the PLL through bit-band), HAL_Delay, HAL_UART_Transmit, EXTI.
// Any failed HAL call lands in Error_Handler, which turns on the blue LED (PD15).
struct HalFirmwareTest : FirmwareTest {
    void SetUp() override {
        FirmwareTest::SetUp();
        if (!IsSkipped())
            load("hal_blinky");
    }
};

TEST_F(HalFirmwareTest, ClockConfigUartAndDelay) {
    // HAL_Delay(100) at 168 MHz is 16.8M cycles; 60M covers the start and a few ticks.
    CpuEngine::StopReason stop;
    const int toggles = countLedToggles(60000000, 0x1, &stop);
    EXPECT_NE(stop, CpuEngine::StopReason::Fault) << m.status();
    EXPECT_FALSE(m.snapshot().leds & 0x8) << "Error_Handler reached, uart: " << uart;
    EXPECT_EQ(uart.rfind("HAL ok, SYSCLK=168000000\r\n", 0), 0u) << "got: " << uart;
    EXPECT_NE(uart.find("tick 3\r\n"), std::string::npos) << "got: " << uart;
    EXPECT_GE(toggles, 3);
}

TEST_F(HalFirmwareTest, ButtonTogglesRedLedThroughExtiCallback) {
    runFor(m, 5000000); // past SystemClock_Config and GPIO_Init
    const bool red = m.snapshot().leds & 0x4;
    m.setButtonPressed(true);
    runFor(m, 200000);
    EXPECT_NE(static_cast<bool>(m.snapshot().leds & 0x4), red) << "press must toggle PD14";
    m.setButtonPressed(false);
    runFor(m, 200000);
    m.setButtonPressed(true);
    runFor(m, 200000);
    EXPECT_EQ(static_cast<bool>(m.snapshot().leds & 0x4), red) << "second press toggles back";
    EXPECT_FALSE(m.snapshot().leds & 0x8) << "Error_Handler reached";
}

// FreeRTOS, port ARM_CM4F: SVC starts the first task, PendSV switches tasks on PSP, SysTick
// ticks at 1 kHz (16k cycles). Two same-priority tasks are time sliced in the middle of float
// loops, so a lost FPU context shows up as a wrong sum. Errors turn on the red LED (PD14).
struct FreeRtosFirmwareTest : FirmwareTest {
    void SetUp() override {
        FirmwareTest::SetUp();
        if (!IsSkipped())
            load("freertos");
    }
};

TEST_F(FreeRtosFirmwareTest, TasksQueueAndFpuContext) {
    // 100 ms = 1.6M cycles per queue item; 8M covers ~5 items and 10 blinks.
    CpuEngine::StopReason stop;
    const int toggles = countLedToggles(8000000, 0x1, &stop);
    EXPECT_NE(stop, CpuEngine::StopReason::Fault) << m.status();
    EXPECT_FALSE(m.snapshot().leds & 0x4) << "error, uart: " << uart;
    EXPECT_EQ(uart.rfind("FreeRTOS start\r\n", 0), 0u) << "got: " << uart;
    EXPECT_NE(uart.find("rx 4 "), std::string::npos) << "got: " << uart;
    EXPECT_EQ(uart.find("a=0 "), std::string::npos) << "cruncher a never ran: " << uart;
    EXPECT_EQ(uart.find("b=0\r"), std::string::npos) << "cruncher b never ran: " << uart;
    EXPECT_GE(toggles, 6);
}

TEST_F(FreeRtosFirmwareTest, ButtonGivesSemaphoreFromIsr) {
    runFor(m, 500000); // scheduler started
    const bool orange = m.snapshot().leds & 0x2;
    m.setButtonPressed(true);
    runFor(m, 100000);
    EXPECT_NE(static_cast<bool>(m.snapshot().leds & 0x2), orange) << "press must toggle PD13";
    m.setButtonPressed(false);
    runFor(m, 100000);
    m.setButtonPressed(true);
    runFor(m, 100000);
    EXPECT_EQ(static_cast<bool>(m.snapshot().leds & 0x2), orange) << "second press toggles back";
    EXPECT_FALSE(m.snapshot().leds & 0x4) << "error, uart: " << uart;
}

// Emulation speed at Max (no pacing), in millions of instructions per second. Not a check, so
// disabled by default; run with:
//   build\tests\mk_emul_tests --gtest_also_run_disabled_tests --gtest_filter=*Speed*
TEST_F(FirmwareTest, DISABLED_Speed) {
    for (const char *name : {"blinky", "uart_printf", "systick_exti", "fpu_irq"}) {
        load(name);
        constexpr uint64_t kInsns = 50000000;
        const auto t0 = std::chrono::steady_clock::now();
        runFor(m, kInsns);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%-14s %6.1f MIPS\n", name, kInsns / sec / 1e6);
    }
}

} // namespace
