#pragma once

#include "periph/Peripheral.h"

#include <array>
#include <cstdint>

class IrqTarget;
class GpioBank;

class Exti : public Peripheral {
public:
    Exti(IrqTarget *nvic);

    const char *name() const override { return "EXTI"; }
    uint32_t base() const override { return 0x40013C00u; }
    uint32_t size() const override { return 0x400u; }

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void reset() override;

    void setSyscfgExticr(int index, uint32_t value);
    uint32_t syscfgExticr(int index) const { return exticr_[index]; }
    void onGpioChange(int port, int pin, bool oldLevel, bool newLevel);
    void setGpioBanks(GpioBank **banks);

private:
    void raiseLine(int line);
    void updateIrqs();
    int portForLine(int line) const;

    IrqTarget *nvic_;
    uint32_t imr_ = 0;
    uint32_t emr_ = 0;
    uint32_t rtsr_ = 0;
    uint32_t ftsr_ = 0;
    uint32_t swier_ = 0;
    uint32_t pr_ = 0;
    std::array<uint32_t, 4> exticr_{};
};

class Syscfg : public Peripheral {
public:
    explicit Syscfg(Exti *exti);

    const char *name() const override { return "SYSCFG"; }
    uint32_t base() const override { return 0x40013800u; }
    uint32_t size() const override { return 0x400u; }

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void reset() override;

private:
    Exti *exti_;
    uint32_t memrmp_ = 0;
    uint32_t pmc_ = 0;
    uint32_t cmpcr_ = 0;
};
