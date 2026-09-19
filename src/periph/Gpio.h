#pragma once

#include "periph/Peripheral.h"

#include <array>
#include <cstdint>
#include <functional>

class Rcc;
class Exti;

class GpioBank : public Peripheral {
public:
    using PinChanged = std::function<void(int port, uint16_t odr, uint16_t idr)>;

    GpioBank(int portIndex, uint32_t base, Rcc *rcc);

    const char *name() const override { return name_; }
    uint32_t base() const override { return base_; }
    uint32_t size() const override { return 0x400u; }

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void reset() override;

    uint16_t odr() const { return odr_; }
    uint16_t idr() const { return idr_; }
    uint32_t moder() const { return moder_; }

    void setExternalInput(int pin, bool high);
    void setPinChanged(PinChanged cb) { onPin_ = std::move(cb); }
    void setExti(Exti *exti) { exti_ = exti; }
    int portIndex() const { return portIndex_; }

private:
    void notify();

    int portIndex_;
    uint32_t base_;
    char name_[8]{};
    Rcc *rcc_;
    Exti *exti_ = nullptr;
    uint32_t moder_ = 0;
    uint32_t otyper_ = 0;
    uint32_t ospeedr_ = 0;
    uint32_t pupdr_ = 0;
    uint16_t idr_ = 0;
    uint16_t odr_ = 0;
    uint32_t lckr_ = 0;
    uint32_t afrl_ = 0;
    uint32_t afrh_ = 0;
    uint16_t external_ = 0;
    PinChanged onPin_;
};
