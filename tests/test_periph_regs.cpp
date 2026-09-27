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

} // namespace
