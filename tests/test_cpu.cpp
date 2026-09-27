// CPU core, breakpoints, exceptions. Programs are hand-assembled Thumb (encodings checked with
// arm-none-eabi-as), so these tests do not need the ARM toolchain.
#include "TestUtil.h"

#include <unicorn/arm.h>

#include <gtest/gtest.h>

namespace {

constexpr uint32_t F = FlashImage::kBase;
constexpr uint32_t kStackTop = 0x20020000u;

// 0x40: loop: adds r0,#1 ; b loop
FlashImage loopImage() {
    FlashImage img;
    img.word(F, kStackTop).vector(1, F + 0x40);
    img.thumb(F + 0x40, {0x3001, 0xE7FD});
    return img;
}

// 0x60: movs r1,#0x60 ; lsls r1,r1,#24 ; ldr r0,[r1] ; b .    (read from 0x60000000, unmapped)
// 0x68: handler: movs r2,#0x42 ; b handler
FlashImage faultImage() {
    FlashImage img;
    img.word(F, kStackTop).vector(1, F + 0x60);
    img.thumb(F + 0x60, {0x2160, 0x0609, 0x6808, 0xE7FE});
    img.thumb(F + 0x68, {0x2242, 0xE7FD});
    return img;
}

// 0x80: SysTick->LOAD = 100; SysTick->CTRL = 3 (HCLK/8, TICKINT, ENABLE); loop: adds r3,#1 ; b loop
// 0x8E: SysTick_Handler: adds r4,#1 ; bx lr
FlashImage sysTickImage() {
    FlashImage img;
    img.word(F, kStackTop).vector(1, F + 0x80).vector(15, F + 0x8E);
    img.thumb(F + 0x80, {0x4904, 0x2064, 0x6048, 0x2003, 0x6008, 0x3301, 0xE7FD, 0x3401, 0x4770});
    img.word(F + 0x94, 0xE000E010u);
    return img;
}

struct MachineTest : ::testing::Test {
    Machine m;

    void SetUp() override {
        std::string err;
        ASSERT_TRUE(m.initialize(&err)) << err;
    }
    uint32_t r(int n) { return m.cpu().reg(UC_ARM_REG_R0 + n); }
    uint32_t ipsr() { return m.cpu().xpsr() & 0x1FFu; }
};

TEST_F(MachineTest, ResetLoadsSpAndPcFromVectorTable) {
    loadImage(m, loopImage());
    EXPECT_EQ(m.cpu().sp(), kStackTop);
    EXPECT_EQ(m.cpu().pc(), F + 0x40);
    EXPECT_EQ(ipsr(), 0u);
}

TEST_F(MachineTest, CountsExactlyTheExecutedInstructions) {
    loadImage(m, loopImage());
    EXPECT_EQ(m.runQuantum(10), CpuEngine::StopReason::Quantum);
    EXPECT_EQ(m.cpu().instructionCount(), 10u);
    EXPECT_EQ(r(0), 5u); // every other instruction is adds
}

TEST_F(MachineTest, StepExecutesOneInstruction) {
    loadImage(m, loopImage());
    m.step();
    m.step();
    m.step();
    EXPECT_EQ(m.cpu().instructionCount(), 3u);
    EXPECT_EQ(r(0), 2u);
    EXPECT_EQ(m.cpu().pc(), F + 0x42);
}

TEST_F(MachineTest, BreakpointStopsBeforeTheInstruction) {
    loadImage(m, loopImage());
    m.addBreakpoint(F + 0x42);

    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Breakpoint);
    EXPECT_EQ(m.cpu().pc(), F + 0x42);
    EXPECT_EQ(m.cpu().instructionCount(), 1u) << "count must stop at the breakpoint, not the quantum";
    EXPECT_EQ(r(0), 1u);

    // Resuming from a breakpoint executes it once and stops on the next hit.
    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Breakpoint);
    EXPECT_EQ(m.cpu().instructionCount(), 3u);
    EXPECT_EQ(r(0), 2u);

    m.removeBreakpoint(F + 0x42);
    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Quantum);
}

// Regression: timers used to advance by the whole quantum even when the CPU stopped early.
TEST_F(MachineTest, TimersAdvanceOnlyByExecutedInstructions) {
    loadImage(m, loopImage());
    m.bus().write(0xE000E014u, 1000, 4); // SysTick LOAD
    m.bus().write(0xE000E010u, 0x5u, 4); // ENABLE | CLKSOURCE, no interrupt
    m.addBreakpoint(F + 0x42);
    ASSERT_EQ(m.runQuantum(5000), CpuEngine::StopReason::Breakpoint);
    EXPECT_EQ(m.snapshot().systickVal, 1000u) << "1 instruction = 1 SysTick tick (reload from 0)";
}

// Regression: the CPU was a Cortex-A15, so returning from an interrupt jumped to 0xFFFFFFF8.
TEST_F(MachineTest, SysTickInterruptEntersAndReturns) {
    loadImage(m, sysTickImage());
    const auto stop = runFor(m, 10000);
    EXPECT_NE(stop, CpuEngine::StopReason::Fault) << m.status();
    // Period = (LOAD + 1) * 8 = 808 cycles -> ~12 interrupts in 10000 instructions.
    // (Pending interrupts are taken between 256-instruction chunks, so periods shorter than
    // that would lose interrupts; see CHANGELOG "known limitations".)
    EXPECT_GE(r(4), 11u);
    EXPECT_LE(r(4), 13u);
    EXPECT_GT(r(3), 4900u) << "thread code must keep running between interrupts";
    EXPECT_EQ(m.cpu().sp(), kStackTop) << "exception frames must be popped";
}

TEST_F(MachineTest, UnmappedReadEntersHardFault) {
    FlashImage img = faultImage();
    img.vector(3, F + 0x68);
    loadImage(m, img);

    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Fault);
    EXPECT_EQ(m.status(), "HardFault: read unmapped @ 0x60000000");
    EXPECT_EQ(m.cpu().pc(), F + 0x68) << "stops at the first handler instruction";
    EXPECT_EQ(ipsr(), 3u);
    EXPECT_EQ(m.cpu().lr(), 0xFFFFFFF9u);

    const auto s = m.snapshot();
    EXPECT_EQ(s.cfsr, 0x00008200u) << "PRECISERR | BFARVALID";
    EXPECT_EQ(s.hfsr, 0x40000000u) << "FORCED";
    EXPECT_EQ(s.bfar, 0x60000000u);

    // Stacked frame: PC = faulting instruction.
    EXPECT_EQ(m.cpu().read32(m.cpu().sp() + 24), F + 0x64);

    // Run continues inside the handler.
    EXPECT_EQ(m.runQuantum(10), CpuEngine::StopReason::Quantum);
    EXPECT_EQ(r(2), 0x42u);
}

TEST_F(MachineTest, BusFaultHandlerWhenEnabledInShcsr) {
    FlashImage img = faultImage();
    img.vector(5, F + 0x68);
    loadImage(m, img);
    m.nvic().write32(0xE000ED24u, 1u << 17); // SHCSR.BUSFAULTENA

    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Fault);
    EXPECT_EQ(m.status(), "BusFault: read unmapped @ 0x60000000");
    EXPECT_EQ(ipsr(), 5u);
    EXPECT_EQ(m.snapshot().hfsr, 0u) << "not escalated";
}

TEST_F(MachineTest, FaultInsideHardFaultIsLockup) {
    FlashImage img = faultImage();
    img.vector(3, F + 0x60); // the HardFault handler itself reads 0x60000000
    loadImage(m, img);

    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Fault);
    EXPECT_EQ(ipsr(), 3u);
    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Fault);
    EXPECT_EQ(m.status().rfind("Lockup", 0), 0u) << m.status();
}

TEST_F(MachineTest, FaultWithoutHandlerIsLockup) {
    loadImage(m, faultImage()); // vector 3 = 0
    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Fault);
    EXPECT_EQ(m.status().rfind("Lockup", 0), 0u) << m.status();
}

TEST_F(MachineTest, UndefinedInstructionIsUsageFaultEscalated) {
    FlashImage img;
    img.word(F, kStackTop).vector(1, F + 0x60).vector(3, F + 0x68);
    img.thumb(F + 0x60, {0xDE00}); // udf #0
    img.thumb(F + 0x68, {0x2242, 0xE7FD});
    loadImage(m, img);

    EXPECT_EQ(m.runQuantum(100), CpuEngine::StopReason::Fault);
    EXPECT_EQ(m.status().rfind("HardFault", 0), 0u) << m.status();
    EXPECT_EQ(m.snapshot().cfsr, 1u << 16) << "UNDEFINSTR";
    EXPECT_EQ(m.snapshot().hfsr, 0x40000000u);
}

} // namespace
