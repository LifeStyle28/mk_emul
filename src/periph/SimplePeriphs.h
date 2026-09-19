#pragma once

#include "periph/Peripheral.h"

#include <cstdint>

class Rcc;
class IrqTarget;

class Timer : public Peripheral {
public:
    Timer(const char *name, uint32_t base, int irq, int timIndex, Rcc *rcc, IrqTarget *nvic);

    const char *name() const override { return name_; }
    uint32_t base() const override { return base_; }
    uint32_t size() const override { return 0x400u; }

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void tick(uint32_t cycles, uint32_t sysclk) override;
    void reset() override;

    uint32_t cnt() const { return cnt_; }
    uint32_t arr() const { return arr_; }
    uint32_t psc() const { return psc_; }
    uint32_t cr1() const { return cr1_; }
    uint32_t ccr1() const { return ccr1_; }

private:
    void updateIrq();
    uint32_t timerClock(uint32_t sysclk) const;

    const char *name_;
    uint32_t base_;
    int irq_;
    int timIndex_;
    Rcc *rcc_;
    IrqTarget *nvic_;
    uint32_t cr1_ = 0;
    uint32_t cr2_ = 0;
    uint32_t smcr_ = 0;
    uint32_t dier_ = 0;
    uint32_t sr_ = 0;
    uint32_t egr_ = 0;
    uint32_t ccmr1_ = 0;
    uint32_t ccmr2_ = 0;
    uint32_t ccer_ = 0;
    uint32_t cnt_ = 0;
    uint32_t psc_ = 0;
    uint32_t arr_ = 0xFFFFFFFFu;
    uint32_t ccr1_ = 0;
    uint32_t ccr2_ = 0;
    uint32_t ccr3_ = 0;
    uint32_t ccr4_ = 0;
    uint64_t frac_ = 0;
};

class Adc : public Peripheral {
public:
    Adc(Rcc *rcc, IrqTarget *nvic);

    const char *name() const override { return "ADC1"; }
    uint32_t base() const override { return 0x40012000u; }
    uint32_t size() const override { return 0x400u; }

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void reset() override;

    void setAnalogValue(uint32_t value12) { analog_ = value12 & 0xFFFu; }
    uint32_t analogValue() const { return analog_; }
    uint32_t dr() const { return dr_; }

private:
    void startConversion();

    Rcc *rcc_;
    IrqTarget *nvic_;
    uint32_t sr_ = 0;
    uint32_t cr1_ = 0;
    uint32_t cr2_ = 0;
    uint32_t smpr1_ = 0;
    uint32_t smpr2_ = 0;
    uint32_t sqr1_ = 0;
    uint32_t sqr2_ = 0;
    uint32_t sqr3_ = 0;
    uint32_t dr_ = 0;
    uint32_t analog_ = 2048;
};

class Spi : public Peripheral {
public:
    Spi(const char *name, uint32_t base, int irq, Rcc *rcc, IrqTarget *nvic);

    const char *name() const override { return name_; }
    uint32_t base() const override { return base_; }
    uint32_t size() const override { return 0x400u; }

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void reset() override;

private:
    void updateIrq();

    const char *name_;
    uint32_t base_;
    int irq_;
    Rcc *rcc_;
    IrqTarget *nvic_;
    uint32_t cr1_ = 0;
    uint32_t cr2_ = 0;
    uint32_t sr_ = 0x0002u; // TXE
    uint32_t dr_ = 0;
};

class I2c : public Peripheral {
public:
    I2c(const char *name, uint32_t base, int evIrq, int erIrq, Rcc *rcc, IrqTarget *nvic);

    const char *name() const override { return name_; }
    uint32_t base() const override { return base_; }
    uint32_t size() const override { return 0x400u; }

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void reset() override;

private:
    const char *name_;
    uint32_t base_;
    int evIrq_;
    int erIrq_;
    Rcc *rcc_;
    IrqTarget *nvic_;
    uint32_t cr1_ = 0;
    uint32_t cr2_ = 0;
    uint32_t oar1_ = 0;
    uint32_t oar2_ = 0;
    uint32_t dr_ = 0;
    uint32_t sr1_ = 0;
    uint32_t sr2_ = 0;
    uint32_t ccr_ = 0;
    uint32_t trise_ = 0;
};

class StubRegs : public Peripheral {
public:
    StubRegs(const char *name, uint32_t base, uint32_t size);

    const char *name() const override { return name_; }
    uint32_t base() const override { return base_; }
    uint32_t size() const override { return size_; }

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void reset() override;

    void setReadValue(uint32_t offset, uint32_t value);

private:
    const char *name_;
    uint32_t base_;
    uint32_t size_;
    uint32_t mem_[64]{};
};
