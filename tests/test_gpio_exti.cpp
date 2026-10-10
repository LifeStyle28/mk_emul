#include "TestUtil.h"

#include "periph/Exti.h"
#include "periph/Gpio.h"

#include <gtest/gtest.h>

namespace {

constexpr uint32_t kModer = 0x00, kIdr = 0x10, kOdr = 0x14, kBsrr = 0x18;
constexpr uint32_t kImr = 0x00, kRtsr = 0x08, kFtsr = 0x0C, kSwier = 0x10, kPr = 0x14;

GpioBank makeGpioD() {
    GpioBank g(3, mcu::kGpioDBase, nullptr);
    g.reset();
    g.write32(mcu::kGpioDBase + kModer, 0x55u << 24); // PD12..PD15 outputs
    return g;
}

TEST(Gpio, BsrrSetsAndResetsPins) {
    GpioBank g = makeGpioD();
    g.write32(mcu::kGpioDBase + kBsrr, 1u << 12);
    EXPECT_EQ(g.odr(), 1u << 12);
    g.write32(mcu::kGpioDBase + kBsrr, (1u << 13) | (1u << 14));
    EXPECT_EQ(g.odr(), (1u << 12) | (1u << 13) | (1u << 14));
    g.write32(mcu::kGpioDBase + kBsrr, (1u << 12) << 16); // BR12
    EXPECT_EQ(g.odr(), (1u << 13) | (1u << 14));
    EXPECT_EQ(g.read32(mcu::kGpioDBase + kBsrr), 0u) << "BSRR is write-only";
}

TEST(Gpio, BsrrSetHasPriorityOverReset) {
    GpioBank g = makeGpioD();
    g.write32(mcu::kGpioDBase + kBsrr, (1u << 15) | ((1u << 15) << 16));
    EXPECT_EQ(g.odr(), 1u << 15);
}

TEST(Gpio, IdrReflectsOutputsAndExternalInputs) {
    GpioBank g = makeGpioD();
    g.write32(mcu::kGpioDBase + kOdr, 1u << 12);
    g.setExternalInput(0, true); // PD0 is an input (MODER=00)
    const uint32_t idr = g.read32(mcu::kGpioDBase + kIdr);
    EXPECT_TRUE(idr & (1u << 12));
    EXPECT_TRUE(idr & (1u << 0));
    g.setExternalInput(0, false);
    EXPECT_FALSE(g.read32(mcu::kGpioDBase + kIdr) & 1u);
}

TEST(Gpio, ExternalInputIgnoredOnOutputPin) {
    GpioBank g = makeGpioD();
    g.setExternalInput(12, true); // PD12 is driven by ODR=0
    EXPECT_FALSE(g.idr() & (1u << 12));
}

struct ExtiFixture : ::testing::Test {
    FakeIrq irq;
    Exti exti{&irq};
    GpioBank gpioA{0, mcu::kGpioABase, nullptr};

    void SetUp() override {
        exti.reset();
        gpioA.reset();
        gpioA.write32(mcu::kGpioABase + kModer, 0); // all inputs
        gpioA.setExti(&exti);
    }
    void w(uint32_t off, uint32_t v) { exti.write32(mcu::kExtiBase + off, v); }
    uint32_t r(uint32_t off) { return exti.read32(mcu::kExtiBase + off); }
};

TEST_F(ExtiFixture, RisingEdgeOnPA0RaisesExti0) {
    w(kImr, 1u);
    w(kRtsr, 1u);
    gpioA.setExternalInput(0, true);
    EXPECT_EQ(r(kPr), 1u);
    EXPECT_TRUE(irq.irq(mcu::kIrqExti0));

    w(kPr, 1u); // rc_w1
    EXPECT_EQ(r(kPr), 0u);
    EXPECT_FALSE(irq.irq(mcu::kIrqExti0));
}

TEST_F(ExtiFixture, FallingEdgeNeedsFtsr) {
    w(kImr, 1u);
    w(kRtsr, 1u);
    gpioA.setExternalInput(0, true);
    w(kPr, 1u);
    gpioA.setExternalInput(0, false); // falling, FTSR not set
    EXPECT_EQ(r(kPr), 0u);

    w(kFtsr, 1u);
    gpioA.setExternalInput(0, true);
    w(kPr, 1u);
    gpioA.setExternalInput(0, false);
    EXPECT_EQ(r(kPr), 1u);
}

TEST_F(ExtiFixture, MaskedLineDoesNotPend) {
    w(kRtsr, 1u); // IMR = 0
    gpioA.setExternalInput(0, true);
    EXPECT_EQ(r(kPr), 0u);
    EXPECT_FALSE(irq.irq(mcu::kIrqExti0));
}

TEST_F(ExtiFixture, LineSelectedFromOtherPortIgnored) {
    exti.setSyscfgExticr(0, 0x3u); // EXTI0 <- PD0
    w(kImr, 1u);
    w(kRtsr, 1u);
    gpioA.setExternalInput(0, true);
    EXPECT_EQ(r(kPr), 0u);
}

TEST_F(ExtiFixture, SharedVectorsForLines5To15) {
    w(kImr, (1u << 7) | (1u << 12));
    w(kSwier, 1u << 7);
    EXPECT_TRUE(irq.irq(mcu::kIrqExti9_5));
    EXPECT_FALSE(irq.irq(mcu::kIrqExti15_10));
    w(kSwier, 1u << 12);
    EXPECT_TRUE(irq.irq(mcu::kIrqExti15_10));
}

} // namespace
