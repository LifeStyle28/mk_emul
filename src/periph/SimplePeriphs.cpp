#include "periph/SimplePeriphs.h"

#include "core/Addresses.h"
#include "periph/Rcc.h"

#include <algorithm>
#include <cstring>

Timer::Timer(const char *name, uint32_t base, int irq, int timIndex, Rcc *rcc, IrqTarget *nvic)
    : name_(name)
    , base_(base)
    , irq_(irq)
    , timIndex_(timIndex)
    , rcc_(rcc)
    , nvic_(nvic) {
    reset();
}

void Timer::reset() {
    cr1_ = 0;
    cr2_ = 0;
    smcr_ = 0;
    dier_ = 0;
    sr_ = 0;
    egr_ = 0;
    ccmr1_ = 0;
    ccmr2_ = 0;
    ccer_ = 0;
    cnt_ = 0;
    psc_ = 0;
    arr_ = 0xFFFFFFFFu;
    ccr1_ = 0;
    ccr2_ = 0;
    ccr3_ = 0;
    ccr4_ = 0;
    frac_ = 0;
}

void Timer::updateIrq() {
    nvic_->setPending(irq_, (sr_ & 1u) && (dier_ & 1u));
}

uint32_t Timer::timerClock(uint32_t sysclk) const {
    (void)sysclk;
    if (timIndex_ == 2 || timIndex_ == 3)
        return rcc_ ? rcc_->timclk1() : 16000000u;
    return rcc_ ? rcc_->timclk2() : 16000000u;
}

uint32_t Timer::read32(uint32_t addr) {
    switch (addr - base_) {
    case 0x00:
        return cr1_;
    case 0x04:
        return cr2_;
    case 0x08:
        return smcr_;
    case 0x0C:
        return dier_;
    case 0x10:
        return sr_;
    case 0x18:
        return ccmr1_;
    case 0x1C:
        return ccmr2_;
    case 0x20:
        return ccer_;
    case 0x24:
        return cnt_;
    case 0x28:
        return psc_;
    case 0x2C:
        return arr_;
    case 0x34:
        return ccr1_;
    case 0x38:
        return ccr2_;
    case 0x3C:
        return ccr3_;
    case 0x40:
        return ccr4_;
    default:
        return 0;
    }
}

void Timer::write32(uint32_t addr, uint32_t value) {
    switch (addr - base_) {
    case 0x00:
        cr1_ = value;
        break;
    case 0x04:
        cr2_ = value;
        break;
    case 0x08:
        smcr_ = value;
        break;
    case 0x0C:
        dier_ = value;
        break;
    case 0x10:
        sr_ &= ~value;
        break;
    case 0x14:
        if (value & 1u) {
            cnt_ = 0;
            sr_ |= 1u;
        }
        break;
    case 0x18:
        ccmr1_ = value;
        break;
    case 0x1C:
        ccmr2_ = value;
        break;
    case 0x20:
        ccer_ = value;
        break;
    case 0x24:
        cnt_ = value;
        break;
    case 0x28:
        psc_ = value;
        break;
    case 0x2C:
        arr_ = value;
        break;
    case 0x34:
        ccr1_ = value;
        break;
    case 0x38:
        ccr2_ = value;
        break;
    case 0x3C:
        ccr3_ = value;
        break;
    case 0x40:
        ccr4_ = value;
        break;
    default:
        break;
    }
    updateIrq();
}

void Timer::tick(uint32_t cycles, uint32_t sysclk) {
    if ((cr1_ & 1u) == 0)
        return;
    if (rcc_ && !rcc_->timEnabled(timIndex_))
        return;
    const uint32_t tclk = timerClock(sysclk);
    const uint32_t hclk = rcc_ ? rcc_->hclk() : sysclk;
    const uint32_t div = psc_ + 1;
    frac_ += static_cast<uint64_t>(cycles) * tclk;
    uint32_t steps = static_cast<uint32_t>(frac_ / (static_cast<uint64_t>(hclk) * div));
    frac_ %= static_cast<uint64_t>(hclk) * div;
    const uint32_t top = arr_ ? arr_ : 0xFFFFu;
    while (steps--) {
        if (cnt_ >= top) {
            cnt_ = 0;
            sr_ |= 1u;
            updateIrq();
        } else {
            ++cnt_;
        }
    }
}

Adc::Adc(Rcc *rcc, IrqTarget *nvic)
    : rcc_(rcc)
    , nvic_(nvic) {
    reset();
}

void Adc::reset() {
    sr_ = 0;
    cr1_ = 0;
    cr2_ = 0;
    smpr1_ = 0;
    smpr2_ = 0;
    sqr1_ = 0;
    sqr2_ = 0;
    sqr3_ = 0;
    dr_ = 0;
}

void Adc::startConversion() {
    dr_ = analog_;
    sr_ |= 0x02u; // EOC
    if (cr1_ & (1u << 5))
        nvic_->setPending(mcu::kIrqAdc, true);
}

uint32_t Adc::read32(uint32_t addr) {
    switch (addr - mcu::kAdc1Base) {
    case 0x00:
        return sr_;
    case 0x04:
        return cr1_;
    case 0x08:
        return cr2_;
    case 0x0C:
        return smpr1_;
    case 0x10:
        return smpr2_;
    case 0x2C:
        return sqr1_;
    case 0x30:
        return sqr2_;
    case 0x34:
        return sqr3_;
    case 0x4C: {
        const uint32_t v = dr_;
        sr_ &= ~0x02u;
        return v;
    }
    default:
        return 0;
    }
}

void Adc::write32(uint32_t addr, uint32_t value) {
    switch (addr - mcu::kAdc1Base) {
    case 0x00:
        sr_ &= ~value;
        break;
    case 0x04:
        cr1_ = value;
        break;
    case 0x08:
        cr2_ = value;
        if (value & (1u << 30))
            startConversion();
        if ((value & 1u) && (value & (1u << 1))) // ADON + CONT first
            startConversion();
        break;
    case 0x0C:
        smpr1_ = value;
        break;
    case 0x10:
        smpr2_ = value;
        break;
    case 0x2C:
        sqr1_ = value;
        break;
    case 0x30:
        sqr2_ = value;
        break;
    case 0x34:
        sqr3_ = value;
        break;
    default:
        break;
    }
}

Spi::Spi(const char *name, uint32_t base, int irq, Rcc *rcc, IrqTarget *nvic)
    : name_(name)
    , base_(base)
    , irq_(irq)
    , rcc_(rcc)
    , nvic_(nvic) {
    reset();
}

void Spi::reset() {
    cr1_ = 0;
    cr2_ = 0;
    sr_ = 0x0002u;
    dr_ = 0;
}

void Spi::updateIrq() {
    const bool irq = ((sr_ & 2u) && (cr2_ & (1u << 7))) || ((sr_ & 1u) && (cr2_ & (1u << 6)));
    nvic_->setPending(irq_, irq);
}

uint32_t Spi::read32(uint32_t addr) {
    switch (addr - base_) {
    case 0x00:
        return cr1_;
    case 0x04:
        return cr2_;
    case 0x08:
        return sr_;
    case 0x0C: {
        sr_ &= ~1u;
        updateIrq();
        return dr_;
    }
    default:
        return 0;
    }
}

void Spi::write32(uint32_t addr, uint32_t value) {
    switch (addr - base_) {
    case 0x00:
        cr1_ = value;
        break;
    case 0x04:
        cr2_ = value;
        break;
    case 0x0C:
        dr_ = static_cast<uint8_t>(value); // echo slave
        sr_ |= 1u | 2u;                   // RXNE | TXE
        break;
    default:
        break;
    }
    updateIrq();
}

I2c::I2c(const char *name, uint32_t base, int evIrq, int erIrq, Rcc *rcc, IrqTarget *nvic)
    : name_(name)
    , base_(base)
    , evIrq_(evIrq)
    , erIrq_(erIrq)
    , rcc_(rcc)
    , nvic_(nvic) {
    reset();
}

void I2c::reset() {
    cr1_ = 0;
    cr2_ = 0;
    oar1_ = 0;
    oar2_ = 0;
    dr_ = 0;
    sr1_ = 0;
    sr2_ = 0;
    ccr_ = 0;
    trise_ = 0x0002u;
}

uint32_t I2c::read32(uint32_t addr) {
    switch (addr - base_) {
    case 0x00:
        return cr1_;
    case 0x04:
        return cr2_;
    case 0x08:
        return oar1_;
    case 0x0C:
        return oar2_;
    case 0x10:
        sr1_ &= ~(1u << 6); // RXNE clear-ish
        return dr_;
    case 0x14:
        return sr1_;
    case 0x18:
        return sr2_;
    case 0x1C:
        return ccr_;
    case 0x20:
        return trise_;
    default:
        return 0;
    }
}

void I2c::write32(uint32_t addr, uint32_t value) {
    switch (addr - base_) {
    case 0x00:
        cr1_ = value;
        if (value & 0x100u) { // START
            sr1_ |= 1u;       // SB
            sr2_ |= 1u;       // MSL
        }
        if (value & 0x200u) { // STOP
            sr1_ &= ~1u;
            sr2_ &= ~1u;
        }
        break;
    case 0x04:
        cr2_ = value;
        break;
    case 0x08:
        oar1_ = value;
        break;
    case 0x0C:
        oar2_ = value;
        break;
    case 0x10:
        dr_ = value;
        sr1_ |= (1u << 7) | (1u << 1); // TXE | ADDR
        sr1_ &= ~1u;
        break;
    case 0x1C:
        ccr_ = value;
        break;
    case 0x20:
        trise_ = value;
        break;
    default:
        break;
    }
    const bool ev = (cr2_ & (1u << 9)) && (sr1_ & 0x83u);
    nvic_->setPending(evIrq_, ev);
    (void)erIrq_;
}

StubRegs::StubRegs(const char *name, uint32_t base, uint32_t size)
    : name_(name)
    , base_(base)
    , size_(size) {
    reset();
}

void StubRegs::reset() {
    std::memset(mem_, 0, sizeof(mem_));
    if (base_ == mcu::kPwrBase)
        mem_[1] = 0x00004000u; // VOSRDY in CSR
    if (base_ == mcu::kFlashRegsBase)
        mem_[0] = 0; // ACR
}

void StubRegs::setReadValue(uint32_t offset, uint32_t value) {
    const uint32_t idx = (offset / 4) % 64;
    mem_[idx] = value;
}

uint32_t StubRegs::read32(uint32_t addr) {
    const uint32_t idx = ((addr - base_) / 4) % 64;
    uint32_t v = mem_[idx];
    if (base_ == mcu::kPwrBase && (addr - base_) == 4)
        v |= 0x4000u; // VOSRDY
    if (base_ == mcu::kIwdgBase && (addr - base_) == 0x0C)
        v = 0xFFFFu; // SR not busy
    return v;
}

void StubRegs::write32(uint32_t addr, uint32_t value) {
    const uint32_t idx = ((addr - base_) / 4) % 64;
    mem_[idx] = value;
}
