// Interrupt timing and Cortex-M exception features. The programs are in tests/asm/*.s,
// assembled into tests/cortex_programs.inc by tests/asm/gen.py.
#include "TestUtil.h"

#include "core/Addresses.h"

#include <unicorn/arm.h>

#include <gtest/gtest.h>

#include <vector>

namespace {

#include "cortex_programs.inc"

constexpr uint32_t F = FlashImage::kBase;
constexpr uint32_t kStackTop = 0x20020000u;
constexpr int kExcIrq0 = 16, kExcIrq1 = 17, kExcSvc = 11, kExcPendSv = 14, kExcSysTick = 15;

FlashImage program(const std::vector<uint16_t> &code) {
    FlashImage img;
    img.word(F, kStackTop).vector(1, F + 0x100);
    img.code(F + 0x100, code);
    return img;
}

struct NvicTest : ::testing::Test {
    Machine m;

    void SetUp() override {
        std::string err;
        ASSERT_TRUE(m.initialize(&err)) << err;
    }
    uint32_t r(int n) { return m.cpu().reg(UC_ARM_REG_R0 + n); }
    uint32_t ipsr() { return m.cpu().xpsr() & 0x1FFu; }
    CpuEngine::StopReason run(uint64_t insns) {
        const auto stop = runFor(m, insns);
        EXPECT_NE(stop, CpuEngine::StopReason::Fault) << m.status();
        return stop;
    }
};

// Latency: an interrupt pended by a register write is taken before the next instruction,
// not at the end of the 256-instruction chunk.
TEST_F(NvicTest, PendedFromCodeIsTakenBeforeNextInstruction) {
    loadImage(m, program(kIspr).vector(kExcIrq0, F + 0x140));
    run(200);
    EXPECT_EQ(r(7), 1u) << "handler ran once";
    EXPECT_EQ(r(6), 0u) << "handler ran before `movs r5, #1`";
    EXPECT_EQ(r(5), 1u);
    EXPECT_EQ(ipsr(), 0u);
}

TEST_F(NvicTest, CpsieTakesPendingInterruptImmediately) {
    loadImage(m, program(kPrimask).vector(kExcIrq0, F + 0x140));
    run(200);
    EXPECT_EQ(r(6), 1u) << "taken right after CPSIE, not while PRIMASK was set";
    EXPECT_EQ(r(5), 2u);
}

TEST_F(NvicTest, BasepriMasksLowerPriorities) {
    loadImage(m, program(kBasepri).vector(kExcIrq0, F + 0x140));
    run(200);
    EXPECT_EQ(r(6), 1u) << "masked by BASEPRI=0x40, taken right after BASEPRI=0";
}

void loadPrigroup(Machine &m, uint32_t aircr) {
    loadImage(m, program(kPrigroup).vector(kExcIrq0, F + 0x140).vector(kExcIrq1, F + 0x160));
    m.cpu().write32(0x20001000u, aircr);
    m.cpu().setReg(UC_ARM_REG_R4, 0x20001000u);
}

TEST_F(NvicTest, HigherPriorityPreemptsByDefault) {
    loadPrigroup(m, 0x05FA0000u); // PRIGROUP 0: all 4 priority bits preempt
    run(300);
    EXPECT_EQ(r(6), 0u) << "IRQ1 (0x40) preempted IRQ0 (0x50)";
}

TEST_F(NvicTest, SameGroupPriorityDoesNotPreempt) {
    loadPrigroup(m, 0x05FA0500u); // PRIGROUP 5: group = bits [7:6], 0x40 and 0x50 are both group 1
    run(300);
    EXPECT_EQ(r(6), 1u) << "IRQ1 waited for IRQ0 (subpriority only orders pending ones)";
    EXPECT_EQ(m.nvic().read32(0xE000ED0Cu) & 0x700u, 0x500u);
}

TEST_F(NvicTest, AircrWriteWithoutKeyIsIgnored) {
    m.nvic().write32(0xE000ED0Cu, 0x00000504u);
    EXPECT_EQ(m.nvic().read32(0xE000ED0Cu), 0xFA050000u);
    EXPECT_FALSE(m.nvic().takeResetRequest());
}

TEST_F(NvicTest, PriorityRegistersImplementFourBits) {
    m.nvic().write32(0xE000E400u, 0xFFFFFFFFu);
    EXPECT_EQ(m.nvic().read32(0xE000E400u), 0xF0F0F0F0u);
}

TEST_F(NvicTest, SvcCallsHandlerAndReturnsAfterIt) {
    loadImage(m, program(kSvc).vector(kExcSvc, F + 0x140));
    run(100);
    EXPECT_EQ(r(4), F + 0x104u) << "stacked PC = instruction after SVC";
    EXPECT_EQ(r(6), 1u);
    EXPECT_EQ(r(5), 2u) << "execution continued after the SVC";
    EXPECT_EQ(ipsr(), 0u);
}

TEST_F(NvicTest, SvcWithPrimaskEscalatesToHardFault) {
    FlashImage img = program(kSvc).vector(kExcSvc, F + 0x140).vector(3, F + 0x140);
    loadImage(m, img);
    m.cpu().setPrimask(1);
    runFor(m, 3);
    EXPECT_EQ(m.snapshot().hfsr, 0x40000000u) << "FORCED";
}

TEST_F(NvicTest, PendSvFromIcsrIsTakenImmediately) {
    loadImage(m, program(kPendsv).vector(kExcPendSv, F + 0x140));
    run(100);
    EXPECT_EQ(r(6), 0u);
    EXPECT_EQ(r(5), 1u);
}

TEST_F(NvicTest, SystemResetRestartsAndKeepsSram) {
    loadImage(m, program(kSysreset));
    run(500);
    EXPECT_EQ(m.cpu().read32(0x20000000u), 3u) << "booted 3 times: 2 NVIC_SystemReset calls";
    EXPECT_EQ(m.cpu().pc(), F + 0x116u) << "parked in `done`";
}

// Regression: with chunked execution a SysTick period shorter than a chunk lost interrupts
// (39 instead of ~98 in 10000 instructions).
TEST_F(NvicTest, SysTickShorterThanChunkLosesNoInterrupts) {
    loadImage(m, program(kSystickFast).vector(kExcSysTick, F + 0x140));
    run(10000);
    // Period 101 cycles, SysTick enabled after 5 setup instructions: floor(9995 / 101) = 98.
    EXPECT_GE(r(4), 97u);
    EXPECT_LE(r(4), 99u);
}

TEST_F(NvicTest, NestedInterruptsReturnToTheRightPlace) {
    // Same program as HigherPriorityPreemptsByDefault, but check the stack unwinds fully.
    loadPrigroup(m, 0x05FA0000u);
    run(300);
    EXPECT_EQ(ipsr(), 0u);
    EXPECT_EQ(m.cpu().sp(), kStackTop);
    EXPECT_EQ(r(5), 1u) << "IRQ0 resumed and finished after IRQ1";
}

// SysTick->VAL and TIMx->CNT are brought up to date on every access, not only between chunks,
// so polling a counter sees it advance by exactly the elapsed cycles.
TEST_F(NvicTest, CountersAreExactWithinChunk) {
    loadImage(m, program(kCounters));
    run(60);
    EXPECT_EQ(r(2) - r(3), 11u) << "SysTick VAL counts down 1 per cycle (10 NOPs + LDR)";
    EXPECT_EQ(r(5) - r(4), 11u) << "TIM2 CNT counts up 1 per cycle";
}

// Enabling a short timer period mid-chunk shortens the chunk: the update interrupt comes after
// ARR+1 = 21 cycles, not at the end of the 256-instruction chunk.
TEST_F(NvicTest, TimerEnabledMidChunkInterruptsOnTime) {
    loadImage(m, program(kTimerArr).vector(16 + mcu::kIrqTim2, F + 0x140));
    run(400);
    EXPECT_EQ(r(7), 1u) << "handler ran once";
    // 21 cycles after CEN: `movs r5, #0` + 10 iterations of the 2-instruction loop.
    EXPECT_GE(r(6), 9u);
    EXPECT_LE(r(6), 11u);
}

// FPU context: interrupting code that uses the FPU pushes the extended frame (S0-S15, FPSCR),
// like the hardware does, so a handler using floats does not corrupt the interrupted code.
TEST_F(NvicTest, FpuContextIsSavedAcrossInterrupt) {
    loadImage(m, program(kFpuFrame).vector(kExcIrq0, F + 0x180));
    run(200);
    EXPECT_EQ(r(7), 0xFFFFFFE9u) << "EXC_RETURN: thread, MSP, extended frame";
    EXPECT_EQ(r(2) & 4u, 0u) << "CONTROL.FPCA is cleared on entry";
    EXPECT_EQ(r(9) & 4u, 4u) << "the handler's FP instruction sets FPCA";
    EXPECT_EQ(m.cpu().reg(UC_ARM_REG_R8), kStackTop - 0x68u) << "extended frame is 26 words";
    EXPECT_EQ(r(5), 0x3F800000u) << "S0 = 1.0 restored";
    EXPECT_EQ(r(6), 0x40400000u) << "S15 = 3.0 restored";
    EXPECT_EQ(r(4), 0x03C00000u) << "FPSCR restored";
    EXPECT_EQ(r(3) & 4u, 4u) << "thread gets FPCA back";
    EXPECT_EQ(m.cpu().sp(), kStackTop);
}

TEST_F(NvicTest, HandlerUsingFpuAfterIntegerCodeUsesBasicFrame) {
    loadImage(m, program(kFpuHandlerOnly).vector(kExcIrq0, F + 0x140));
    run(100);
    EXPECT_EQ(r(7), 0xFFFFFFF9u);
    EXPECT_EQ(r(5), 0x40000000u) << "handler computed 2.0";
    EXPECT_EQ(r(3) & 4u, 0u) << "FPCA does not leak from the handler into the thread";
    EXPECT_EQ(r(4), kStackTop);
}

TEST_F(NvicTest, NestedHandlerRestoresFpuContextOfPreemptedHandler) {
    loadImage(m, program(kFpuNested).vector(kExcIrq0, F + 0x140).vector(kExcIrq1, F + 0x180));
    run(200);
    EXPECT_EQ(r(7), 0xFFFFFFE1u) << "EXC_RETURN: handler, extended frame";
    EXPECT_EQ(r(5), 0x40A00000u) << "IRQ0 sees its S1 = 5.0 after IRQ1";
    EXPECT_EQ(r(4), kStackTop);
    EXPECT_EQ(m.cpu().sp(), kStackTop);
    EXPECT_EQ(ipsr(), 0u);
}

// FPU access is controlled by SCB->CPACR (0 after reset): an FP instruction without access
// raises a precise NOCP UsageFault, escalated to HardFault unless UsageFault is enabled.
constexpr uint32_t kCfsrNocp = 1u << 19;

TEST_F(NvicTest, FpuDisabledByCpacrIsNocpHardFault) {
    loadImage(m, program(kNocp).vector(3, F + 0x180));
    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Fault);
    EXPECT_EQ(m.status().rfind("HardFault: FPU disabled", 0), 0u) << m.status();
    EXPECT_EQ(m.snapshot().cfsr, kCfsrNocp);
    EXPECT_EQ(m.snapshot().hfsr, 0x40000000u) << "FORCED";
    m.runQuantum(20); // Run from the vector catch: the handler records the stacked PC
    EXPECT_EQ(r(6), r(4)) << "stacked PC = the FP instruction";
    EXPECT_EQ(r(7), 3u);
    EXPECT_EQ(r(5), 0u) << "the instruction after it never ran";
}

TEST_F(NvicTest, FpuDisabledByCpacrIsUsageFaultWhenEnabled) {
    loadImage(m, program(kNocp).vector(6, F + 0x180));
    m.cpu().setReg(UC_ARM_REG_R0, 1); // enable UsageFault in SHCSR
    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Fault);
    EXPECT_EQ(m.status().rfind("UsageFault: FPU disabled", 0), 0u) << m.status();
    EXPECT_EQ(m.snapshot().cfsr, kCfsrNocp);
    EXPECT_EQ(m.snapshot().hfsr, 0u) << "not escalated";
    m.runQuantum(20);
    EXPECT_EQ(r(6), r(4));
    EXPECT_EQ(r(7), 6u);
}

TEST_F(NvicTest, CpacrPrivilegedOnlyFaultsInUnprivilegedThread) {
    loadImage(m, program(kCpacrPriv).vector(3, F + 0x180));
    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Fault);
    EXPECT_EQ(r(5), 0x3F800000u) << "privileged FP instruction worked";
    EXPECT_EQ(m.snapshot().cfsr, kCfsrNocp);
    m.runQuantum(20);
    EXPECT_EQ(r(6), r(4)) << "fault on the unprivileged FP instruction";
    EXPECT_EQ(m.cpu().sp(), kStackTop - 0x20u) << "frame on MSP, handler runs on it";
}

// Thread on PSP, privileged and then unprivileged (as RTOS tasks): the frame goes onto PSP,
// the handler runs on MSP, and the return restores PSP, SPSEL and nPRIV.
TEST_F(NvicTest, ExceptionsFromPspThreadPrivilegedAndUnprivileged) {
    constexpr uint32_t kPsp = 0x2001F000u;
    loadImage(m, program(kPspThread).vector(kExcIrq0, F + 0x180).vector(kExcSvc, F + 0x1C0));
    run(100);
    EXPECT_EQ(r(7), 0xFFFFFFFDu) << "IRQ0 EXC_RETURN: thread, PSP";
    EXPECT_EQ(r(6), kStackTop) << "IRQ0 handler on MSP";
    EXPECT_EQ(r(5), kPsp - 0x20u) << "frame pushed onto PSP";
    EXPECT_EQ(r(4), kPsp) << "back on PSP after IRQ0";
    EXPECT_EQ(r(3), 2u);
    EXPECT_EQ(m.cpu().reg(UC_ARM_REG_R11), 0xFFFFFFFDu) << "SVC from unprivileged thread";
    EXPECT_EQ(m.cpu().reg(UC_ARM_REG_R10), kStackTop) << "SVC handler on MSP";
    EXPECT_EQ(m.cpu().reg(UC_ARM_REG_R8), kPsp) << "back on PSP after SVC";
    EXPECT_EQ(m.cpu().reg(UC_ARM_REG_R9), 3u) << "still unprivileged, on PSP";
    EXPECT_EQ(m.cpu().psp(), kPsp) << "PSP readable for the UI even in unprivileged mode";
    EXPECT_EQ(m.cpu().msp(), kStackTop);
}

} // namespace
