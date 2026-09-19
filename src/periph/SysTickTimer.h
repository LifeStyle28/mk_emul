#pragma once

#include "periph/Peripheral.h"

#include <cstdint>

class IrqTarget;
class Rcc;

class SysTickTimer : public Peripheral {
public:
    SysTickTimer(IrqTarget *nvic, Rcc *rcc);

    const char *name() const override { return "SysTick"; }
    uint32_t base() const override { return 0xE000E010u; }
    uint32_t size() const override { return 0x10u; }

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void tick(uint32_t cycles, uint32_t sysclk) override;
    void reset() override;

    uint32_t ctrl() const { return ctrl_; }
    uint32_t load() const { return load_; }
    uint32_t val() const { return val_; }

private:
    IrqTarget *nvic_;
    Rcc *rcc_;
    uint32_t ctrl_ = 0;
    uint32_t load_ = 0;
    uint32_t val_ = 0;
    uint32_t calib_ = 0x00001680u;
    uint64_t frac_ = 0;
};
