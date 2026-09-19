#include "periph/SysTickTimer.h"

#include "periph/Rcc.h"

SysTickTimer::SysTickTimer(IrqTarget *nvic, Rcc *rcc)
    : nvic_(nvic)
    , rcc_(rcc) {
    reset();
}

void SysTickTimer::reset() {
    ctrl_ = 0;
    load_ = 0;
    val_ = 0;
    calib_ = 16000;
    frac_ = 0;
}

uint32_t SysTickTimer::read32(uint32_t addr) {
    switch (addr - 0xE000E010u) {
    case 0x00: {
        const uint32_t v = ctrl_;
        ctrl_ &= ~(1u << 16); // COUNTFLAG clears on read
        return v;
    }
    case 0x04:
        return load_;
    case 0x08:
        return val_;
    case 0x0C:
        return calib_;
    default:
        return 0;
    }
}

void SysTickTimer::write32(uint32_t addr, uint32_t value) {
    switch (addr - 0xE000E010u) {
    case 0x00:
        ctrl_ = (ctrl_ & (1u << 16)) | (value & 0x7u);
        break;
    case 0x04:
        load_ = value & 0x00FFFFFFu;
        break;
    case 0x08:
        val_ = 0;
        break;
    default:
        break;
    }
}

void SysTickTimer::tick(uint32_t cycles, uint32_t sysclk) {
    if ((ctrl_ & 1u) == 0 || load_ == 0)
        return;
    uint32_t tickHz = (ctrl_ & 4u) ? (rcc_ ? rcc_->hclk() : sysclk) : (rcc_ ? rcc_->hclk() / 8 : sysclk / 8);
    if (tickHz == 0)
        tickHz = 1;
    // 1 instruction ≈ 1 HCLK cycle
    const uint64_t hclk = rcc_ ? rcc_->hclk() : sysclk;
    frac_ += static_cast<uint64_t>(cycles) * tickHz;
    uint32_t ticks = static_cast<uint32_t>(frac_ / hclk);
    frac_ %= hclk;
    while (ticks--) {
        if (val_ == 0)
            val_ = load_;
        else
            --val_;
        if (val_ == 0) {
            val_ = load_;
            ctrl_ |= (1u << 16);
            if (ctrl_ & 2u)
                nvic_->setSystemPending(15, true);
        }
    }
}
