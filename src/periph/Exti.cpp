#include "periph/Exti.h"

#include "core/Addresses.h"
#include "periph/Gpio.h"

Exti::Exti(IrqTarget *nvic)
    : nvic_(nvic) {
    reset();
}

void Exti::reset() {
    imr_ = 0;
    emr_ = 0;
    rtsr_ = 0;
    ftsr_ = 0;
    swier_ = 0;
    pr_ = 0;
    exticr_.fill(0);
}

int Exti::portForLine(int line) const {
    const int reg = line / 4;
    const int shift = (line % 4) * 4;
    return static_cast<int>((exticr_[reg] >> shift) & 0xFu);
}

void Exti::setSyscfgExticr(int index, uint32_t value) {
    if (index >= 0 && index < 4)
        exticr_[index] = value;
}

void Exti::raiseLine(int line) {
    const uint32_t bit = 1u << line;
    if (imr_ & bit) {
        pr_ |= bit;
        updateIrqs();
    }
}

void Exti::updateIrqs() {
    auto pendingMasked = pr_ & imr_;
    nvic_->setPending(mcu::kIrqExti0, pendingMasked & (1u << 0));
    nvic_->setPending(mcu::kIrqExti1, pendingMasked & (1u << 1));
    nvic_->setPending(mcu::kIrqExti2, pendingMasked & (1u << 2));
    nvic_->setPending(mcu::kIrqExti3, pendingMasked & (1u << 3));
    nvic_->setPending(mcu::kIrqExti4, pendingMasked & (1u << 4));
    nvic_->setPending(mcu::kIrqExti9_5, pendingMasked & 0x3E0u);
    nvic_->setPending(mcu::kIrqExti15_10, pendingMasked & 0xFC00u);
}

void Exti::onGpioChange(int port, int pin, bool oldLevel, bool newLevel) {
    if (portForLine(pin) != port)
        return;
    const uint32_t bit = 1u << pin;
    if (!oldLevel && newLevel && (rtsr_ & bit))
        raiseLine(pin);
    if (oldLevel && !newLevel && (ftsr_ & bit))
        raiseLine(pin);
}

void Exti::setGpioBanks(GpioBank **) {}

uint32_t Exti::read32(uint32_t addr) {
    switch (addr - mcu::kExtiBase) {
    case 0x00:
        return imr_;
    case 0x04:
        return emr_;
    case 0x08:
        return rtsr_;
    case 0x0C:
        return ftsr_;
    case 0x10:
        return swier_;
    case 0x14:
        return pr_;
    default:
        return 0;
    }
}

void Exti::write32(uint32_t addr, uint32_t value) {
    switch (addr - mcu::kExtiBase) {
    case 0x00:
        imr_ = value;
        break;
    case 0x04:
        emr_ = value;
        break;
    case 0x08:
        rtsr_ = value;
        break;
    case 0x0C:
        ftsr_ = value;
        break;
    case 0x10:
        swier_ = value;
        for (int i = 0; i < 23; ++i) {
            if (swier_ & (1u << i))
                raiseLine(i);
        }
        swier_ = 0;
        break;
    case 0x14:
        pr_ &= ~value;
        break;
    default:
        break;
    }
    updateIrqs();
}

Syscfg::Syscfg(Exti *exti)
    : exti_(exti) {
    reset();
}

void Syscfg::reset() {
    memrmp_ = 0;
    pmc_ = 0;
    cmpcr_ = 0x00000100u; // READY
}

uint32_t Syscfg::read32(uint32_t addr) {
    switch (addr - mcu::kSyscfgBase) {
    case 0x00:
        return memrmp_;
    case 0x04:
        return pmc_;
    case 0x08:
        return exti_->syscfgExticr(0);
    case 0x0C:
        return exti_->syscfgExticr(1);
    case 0x10:
        return exti_->syscfgExticr(2);
    case 0x14:
        return exti_->syscfgExticr(3);
    case 0x20:
        return cmpcr_;
    default:
        return 0;
    }
}

void Syscfg::write32(uint32_t addr, uint32_t value) {
    switch (addr - mcu::kSyscfgBase) {
    case 0x00:
        memrmp_ = value;
        break;
    case 0x04:
        pmc_ = value;
        break;
    case 0x08:
        exti_->setSyscfgExticr(0, value);
        break;
    case 0x0C:
        exti_->setSyscfgExticr(1, value);
        break;
    case 0x10:
        exti_->setSyscfgExticr(2, value);
        break;
    case 0x14:
        exti_->setSyscfgExticr(3, value);
        break;
    case 0x20:
        cmpcr_ = value | 0x100u;
        break;
    default:
        break;
    }
}
