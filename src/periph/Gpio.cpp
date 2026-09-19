#include "periph/Gpio.h"

#include "core/Addresses.h"
#include "periph/Exti.h"
#include "periph/Rcc.h"

#include <cstdio>

GpioBank::GpioBank(int portIndex, uint32_t base, Rcc *rcc)
    : portIndex_(portIndex)
    , base_(base)
    , rcc_(rcc) {
    std::snprintf(name_, sizeof(name_), "GPIO%c", static_cast<char>('A' + portIndex));
}

void GpioBank::reset() {
    moder_ = (portIndex_ == 0) ? 0xA8000000u : (portIndex_ == 1) ? 0x00000280u : 0;
    otyper_ = 0;
    ospeedr_ = (portIndex_ == 0) ? 0x0C000000u : (portIndex_ == 1) ? 0x000000C0u : 0;
    pupdr_ = (portIndex_ == 0) ? 0x64000000u : (portIndex_ == 1) ? 0x00000100u : 0;
    odr_ = 0;
    idr_ = external_;
    lckr_ = 0;
    afrl_ = 0;
    afrh_ = 0;
}

void GpioBank::notify() {
    if (onPin_)
        onPin_(portIndex_, odr_, idr_);
}

void GpioBank::setExternalInput(int pin, bool high) {
    const uint16_t mask = static_cast<uint16_t>(1u << pin);
    const uint16_t old = idr_;
    if (high)
        external_ |= mask;
    else
        external_ &= ~mask;
    idr_ = static_cast<uint16_t>((odr_ & static_cast<uint16_t>(moder_ == 0 ? 0 : 0xFFFF)) | external_);
    // IDR: inputs from external, outputs reflect ODR
    uint16_t idr = 0;
    for (int i = 0; i < 16; ++i) {
        const uint32_t mode = (moder_ >> (2 * i)) & 3u;
        if (mode == 1) // output
            idr |= static_cast<uint16_t>(((odr_ >> i) & 1u) << i);
        else
            idr |= static_cast<uint16_t>(((external_ >> i) & 1u) << i);
    }
    idr_ = idr;
    if (exti_ && ((old ^ idr_) & mask))
        exti_->onGpioChange(portIndex_, pin, (old & mask) != 0, (idr_ & mask) != 0);
    notify();
}

uint32_t GpioBank::read32(uint32_t addr) {
    switch (addr - base_) {
    case 0x00:
        return moder_;
    case 0x04:
        return otyper_;
    case 0x08:
        return ospeedr_;
    case 0x0C:
        return pupdr_;
    case 0x10:
        return idr_;
    case 0x14:
        return odr_;
    case 0x18:
        return 0;
    case 0x1C:
        return lckr_;
    case 0x20:
        return afrl_;
    case 0x24:
        return afrh_;
    default:
        return 0;
    }
}

void GpioBank::write32(uint32_t addr, uint32_t value) {
    if (rcc_ && !rcc_->gpioEnabled(portIndex_)) {
        // still accept writes so misconfigured firmware doesn't hard-fault; clocks affect tick only
    }
    const uint32_t off = addr - base_;
    const uint16_t oldOdr = odr_;
    switch (off) {
    case 0x00:
        moder_ = value;
        break;
    case 0x04:
        otyper_ = value;
        break;
    case 0x08:
        ospeedr_ = value;
        break;
    case 0x0C:
        pupdr_ = value;
        break;
    case 0x14:
        odr_ = static_cast<uint16_t>(value);
        break;
    case 0x18:
        odr_ = static_cast<uint16_t>((odr_ | (value & 0xFFFFu)) & ~((value >> 16) & 0xFFFFu));
        break;
    case 0x1C:
        lckr_ = value;
        break;
    case 0x20:
        afrl_ = value;
        break;
    case 0x24:
        afrh_ = value;
        break;
    default:
        break;
    }
    uint16_t idr = 0;
    for (int i = 0; i < 16; ++i) {
        const uint32_t mode = (moder_ >> (2 * i)) & 3u;
        if (mode == 1)
            idr |= static_cast<uint16_t>(((odr_ >> i) & 1u) << i);
        else
            idr |= static_cast<uint16_t>(((external_ >> i) & 1u) << i);
    }
    const uint16_t oldIdr = idr_;
    idr_ = idr;
    if (exti_) {
        const uint16_t diff = oldIdr ^ idr_;
        for (int i = 0; i < 16; ++i) {
            if (diff & (1u << i))
                exti_->onGpioChange(portIndex_, i, (oldIdr & (1u << i)) != 0, (idr_ & (1u << i)) != 0);
        }
    }
    if (odr_ != oldOdr)
        notify();
}
