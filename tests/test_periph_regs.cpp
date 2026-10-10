#include "TestUtil.h"

#include "core/MmioBus.h"
#include "periph/Exti.h"
#include "periph/SimplePeriphs.h"
#include "periph/SysTickTimer.h"
#include "periph/Usart.h"

#include <gtest/gtest.h>

#include <string>

namespace {

constexpr uint32_t kUsartSr = mcu::kUsart2Base + 0x00;
constexpr uint32_t kUsartDr = mcu::kUsart2Base + 0x04;
constexpr uint32_t kRxne = 1u << 5, kTc = 1u << 6, kTxe = 1u << 7;

struct UsartFixture : ::testing::Test {
    FakeIrq irq;
    Usart usart{"USART2", mcu::kUsart2Base, mcu::kIrqUsart2, nullptr, &irq, 2};
    MmioBus bus;
    std::string tx;

    void SetUp() override {
        usart.reset();
        usart.setTxCallback([this](uint8_t b) { tx.push_back(static_cast<char>(b)); });
        bus.add(&usart);
    }
};

// Regression: a byte write to DR used to read DR first, which popped the RX FIFO.
TEST_F(UsartFixture, ByteWriteToDrKeepsReceivedByte) {
    usart.pushRx('A');
    bus.write(kUsartDr, 'x', 1);
    EXPECT_EQ(tx, "x");
    EXPECT_TRUE(bus.read(kUsartSr, 4) & kRxne) << "RXNE lost by the partial write";
    EXPECT_EQ(bus.read(kUsartDr, 4), static_cast<uint32_t>('A'));
    EXPECT_FALSE(bus.read(kUsartSr, 4) & kRxne);
}

TEST_F(UsartFixture, RxFifoOrderAndRxneFlag) {
    usart.pushRx('1');
    usart.pushRx('2');
    EXPECT_EQ(bus.read(kUsartDr, 4), static_cast<uint32_t>('1'));
    EXPECT_TRUE(bus.read(kUsartSr, 4) & kRxne);
    EXPECT_EQ(bus.read(kUsartDr, 4), static_cast<uint32_t>('2'));
    EXPECT_FALSE(bus.read(kUsartSr, 4) & kRxne);
}

// SR flags are rc_w0: HAL clears TC with SR = ~TC; other flags must survive.
TEST_F(UsartFixture, SrIsClearedByWritingZero) {
    usart.pushRx('A');
    bus.write(kUsartDr, 'x', 4); // sets TC | TXE
    bus.write(kUsartSr, ~kTc, 4);
    const uint32_t sr = bus.read(kUsartSr, 4);
    EXPECT_FALSE(sr & kTc);
    EXPECT_TRUE(sr & kRxne);
    EXPECT_TRUE(sr & kTxe);
}

TEST_F(UsartFixture, RxneInterruptWhenEnabled) {
    bus.write(mcu::kUsart2Base + 0x0C, (1u << 13) | (1u << 5), 4); // UE | RXNEIE
    usart.pushRx('z');
    EXPECT_TRUE(irq.irq(mcu::kIrqUsart2));
    bus.read(kUsartDr, 4);
    EXPECT_FALSE(irq.irq(mcu::kIrqUsart2));
}

struct TimerFixture : ::testing::Test {
    FakeIrq irq;
    Timer tim{"TIM2", mcu::kTim2Base, mcu::kIrqTim2, 2, nullptr, &irq};
    uint32_t sr() { return tim.read32(mcu::kTim2Base + 0x10); }
    void w(uint32_t off, uint32_t v) { tim.write32(mcu::kTim2Base + off, v); }
};

// Regression: TIMx->SR = ~TIM_SR_UIF used to clear everything except UIF -> endless IRQ.
TEST_F(TimerFixture, SrIsClearedByWritingZero) {
    w(0x0C, 1u); // DIER.UIE
    w(0x14, 1u); // EGR.UG -> UIF
    ASSERT_TRUE(sr() & 1u);
    EXPECT_TRUE(irq.irq(mcu::kIrqTim2));

    w(0x10, ~2u); // clear CC1IF only
    EXPECT_TRUE(sr() & 1u);
    w(0x10, ~1u); // clear UIF
    EXPECT_FALSE(sr() & 1u);
    EXPECT_FALSE(irq.irq(mcu::kIrqTim2));
}

TEST_F(TimerFixture, CountsWithPrescalerAndOverflows) {
    w(0x28, 15); // PSC: 16 MHz / 16 = 1 MHz
    w(0x2C, 9);  // ARR: 10 counts per period
    w(0x00, 1);  // CEN
    // No RCC: timer clock = sysclk, one instruction = one cycle.
    tim.tick(16 * 5, 16000000u);
    EXPECT_EQ(tim.cnt(), 5u);
    EXPECT_FALSE(sr() & 1u);
    tim.tick(16 * 5, 16000000u);
    EXPECT_EQ(tim.cnt(), 0u);
    EXPECT_TRUE(sr() & 1u) << "update event on overflow";
}

// tick() jumps over many periods at once; the result must match counting one by one.
TEST_F(TimerFixture, LongTickWrapsLikeSingleSteps) {
    w(0x2C, 9); // ARR
    w(0x00, 1); // CEN
    tim.tick(3, 16000000u);
    tim.tick(1234, 16000000u); // 3 + 1234 = 1237 = 123 periods + 7
    EXPECT_EQ(tim.cnt(), 7u);
    EXPECT_TRUE(sr() & 1u);
    w(0x10, 0);
    w(0x24, 20); // CNT above ARR: the next step wraps to 0
    tim.tick(4, 16000000u);
    EXPECT_EQ(tim.cnt(), 3u);
    EXPECT_TRUE(sr() & 1u);
}

TEST(Timer, SixteenBitTimersMaskCounterRegisters) {
    FakeIrq irq;
    Timer tim4{"TIM4", mcu::kTim4Base, mcu::kIrqTim4, 4, nullptr, &irq};
    EXPECT_EQ(tim4.arr(), 0xFFFFu) << "reset value of a 16-bit ARR";
    tim4.write32(mcu::kTim4Base + 0x2C, 0x12345678u);
    EXPECT_EQ(tim4.arr(), 0x5678u);
    Timer tim5{"TIM5", mcu::kTim5Base, mcu::kIrqTim5, 5, nullptr, &irq};
    EXPECT_EQ(tim5.arr(), 0xFFFFFFFFu);
}

TEST_F(TimerFixture, PwmOutputLevel) {
    EXPECT_LT(tim.outputLevel(1), 0) << "CC1E off: not an output";
    w(0x2C, 999);                       // ARR: period 1000
    w(0x18, (6u << 4) | (7u << 12));    // CCMR1: CH1 PWM mode 1, CH2 PWM mode 2
    w(0x1C, (4u << 4) | (5u << 12));    // CCMR2: CH3 forced inactive, CH4 forced active
    w(0x20, 0x1111u | (1u << 13));      // CCER: CC1E-CC4E, CC4P (active low)
    w(0x34, 250);                       // CCR1
    w(0x38, 250);                       // CCR2
    w(0x00, 1);                         // CEN
    EXPECT_DOUBLE_EQ(tim.outputLevel(1), 0.25);
    EXPECT_DOUBLE_EQ(tim.outputLevel(2), 0.75);
    EXPECT_DOUBLE_EQ(tim.outputLevel(3), 0.0);
    EXPECT_DOUBLE_EQ(tim.outputLevel(4), 0.0) << "forced active, inverted by CC4P";
    w(0x34, 5000);                      // CCR above ARR: always active
    EXPECT_DOUBLE_EQ(tim.outputLevel(1), 1.0);
    w(0x18, (6u << 4) | 1u);            // CC1S = 01: channel 1 is an input
    EXPECT_LT(tim.outputLevel(1), 0);
}

TEST(Exti, ByteWriteToPrClearsOnlyThatByte) {
    FakeIrq irq;
    Exti exti(&irq);
    exti.reset();
    MmioBus bus;
    bus.add(&exti);
    bus.write(mcu::kExtiBase + 0x00, (1u << 0) | (1u << 8), 4); // IMR
    bus.write(mcu::kExtiBase + 0x10, (1u << 0) | (1u << 8), 4); // SWIER
    ASSERT_EQ(bus.read(mcu::kExtiBase + 0x14, 4), (1u << 0) | (1u << 8));
    bus.write(mcu::kExtiBase + 0x14, 1u, 1); // PR byte 0 = 1
    EXPECT_EQ(bus.read(mcu::kExtiBase + 0x14, 4), 1u << 8);
}

struct SysTickFixture : ::testing::Test {
    FakeIrq irq;
    SysTickTimer st{&irq, nullptr};
    void w(uint32_t off, uint32_t v) { st.write32(0xE000E010u + off, v); }
    uint32_t r(uint32_t off) { return st.read32(0xE000E010u + off); }
};

TEST_F(SysTickFixture, WrapsSetsCountflagAndPendsException) {
    w(0x04, 9);     // LOAD
    w(0x00, 0x7u);  // ENABLE | TICKINT | CLKSOURCE=processor
    st.tick(9, 16000000u);
    EXPECT_FALSE(irq.sys(15));
    st.tick(1, 16000000u);
    EXPECT_TRUE(irq.sys(15));
    EXPECT_TRUE(r(0x00) & (1u << 16));
    EXPECT_FALSE(r(0x00) & (1u << 16)) << "COUNTFLAG clears on read";
}

TEST_F(SysTickFixture, ExternalClockIsHclkDiv8) {
    w(0x04, 99);
    w(0x00, 0x3u); // CLKSOURCE = HCLK/8
    st.tick(8 * 50, 16000000u);
    EXPECT_EQ(st.val(), 50u);
}

TEST_F(SysTickFixture, WriteToValClearsCountflag) {
    w(0x04, 1);
    w(0x00, 0x5u);
    st.tick(2, 16000000u);
    w(0x08, 123);
    EXPECT_EQ(r(0x08), 0u);
    EXPECT_FALSE(r(0x00) & (1u << 16));
}

TEST(Adc, ConversionReadsSliderValueAndClearsEoc) {
    FakeIrq irq;
    Adc adc(nullptr, &irq);
    adc.setAnalogValue(3000);
    adc.write32(mcu::kAdc1Base + 0x08, 1u | (1u << 30)); // ADON | SWSTART
    EXPECT_TRUE(adc.read32(mcu::kAdc1Base + 0x00) & 2u);
    EXPECT_EQ(adc.read32(mcu::kAdc1Base + 0x4C), 3000u);
    EXPECT_FALSE(adc.read32(mcu::kAdc1Base + 0x00) & 2u);
}

uint32_t bitbandAlias(uint32_t aliasBase, uint32_t targetBase, uint32_t addr, unsigned bit) {
    return aliasBase + (addr - targetBase) * 32u + bit * 4u;
}

// HAL sets e.g. RCC->CR.PLLON through the bit-band alias (__HAL_RCC_PLL_ENABLE).
TEST(Bitband, PeripheralAndSramBits) {
    Machine m;
    std::string err;
    ASSERT_TRUE(m.initialize(&err)) << err;
    auto &bus = m.bus();
    const auto periph = [](uint32_t addr, unsigned bit) {
        return bitbandAlias(mcu::kPeriphBitbandBase, mcu::kApbAhbBase, addr, bit);
    };

    constexpr uint32_t kAhb1enr = mcu::kRccBase + 0x30;
    const uint32_t before = bus.read(kAhb1enr, 4);
    bus.write(periph(kAhb1enr, 3), 1, 4); // GPIODEN
    EXPECT_EQ(bus.read(kAhb1enr, 4), before | (1u << 3));
    EXPECT_EQ(bus.read(periph(kAhb1enr, 3), 4), 1u);

    constexpr uint32_t kGpiodOdr = mcu::kGpioDBase + 0x14;
    bus.write(periph(kGpiodOdr, 13), 1, 4);
    EXPECT_EQ(bus.read(kGpiodOdr, 4), 1u << 13);
    bus.write(periph(kGpiodOdr, 13), 0, 4);
    EXPECT_EQ(bus.read(kGpiodOdr, 4), 0u);
    EXPECT_EQ(bus.read(periph(kGpiodOdr, 13), 4), 0u);

    constexpr uint32_t kWord = mcu::kSramBase + 0x100;
    m.cpu().write32(kWord, 0x80000000u);
    bus.write(bitbandAlias(mcu::kSramBitbandBase, mcu::kSramBase, kWord + 1, 2), 1, 4); // bit 10
    EXPECT_EQ(m.cpu().read32(kWord), 0x80000400u);
    EXPECT_EQ(bus.read(bitbandAlias(mcu::kSramBitbandBase, mcu::kSramBase, kWord, 31), 4), 1u);
}

} // namespace
