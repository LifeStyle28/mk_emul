#pragma once

#include "periph/Peripheral.h"

#include "core/Addresses.h"

#include <cstdint>
#include <functional>

class Rcc : public Peripheral {
public:
    using ClockChanged = std::function<void()>;

    const char *name() const override { return "RCC"; }
    uint32_t base() const override { return mcu::kRccBase; }
    uint32_t size() const override { return 0x400u; }

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void reset() override;

    uint32_t sysclk() const;
    uint32_t hclk() const;
    uint32_t pclk1() const;
    uint32_t pclk2() const;
    uint32_t timclk1() const;
    uint32_t timclk2() const;

    bool gpioEnabled(int portIndex) const;
    bool usartEnabled(int index) const; // 1,2,3,6
    bool timEnabled(int index) const;
    bool adcEnabled() const;
    bool spi1Enabled() const;
    bool i2c1Enabled() const;

    uint32_t cr() const { return cr_; }
    uint32_t cfgr() const { return cfgr_; }
    uint32_t pllcfgr() const { return pllcfgr_; }
    uint32_t ahb1enr() const { return ahb1enr_; }
    uint32_t apb1enr() const { return apb1enr_; }
    uint32_t apb2enr() const { return apb2enr_; }

    void setClockChanged(ClockChanged cb) { onClock_ = std::move(cb); }

private:
    static uint32_t prescaleAhb(uint32_t hpre);
    static uint32_t prescaleApb(uint32_t ppre);

    uint32_t cr_ = 0;
    uint32_t pllcfgr_ = 0;
    uint32_t cfgr_ = 0;
    uint32_t cir_ = 0;
    uint32_t ahb1rstr_ = 0;
    uint32_t ahb2rstr_ = 0;
    uint32_t ahb3rstr_ = 0;
    uint32_t apb1rstr_ = 0;
    uint32_t apb2rstr_ = 0;
    uint32_t ahb1enr_ = 0;
    uint32_t ahb2enr_ = 0;
    uint32_t ahb3enr_ = 0;
    uint32_t apb1enr_ = 0;
    uint32_t apb2enr_ = 0;
    uint32_t ahb1lpenr_ = 0;
    uint32_t ahb2lpenr_ = 0;
    uint32_t ahb3lpenr_ = 0;
    uint32_t apb1lpenr_ = 0;
    uint32_t apb2lpenr_ = 0;
    uint32_t bdcr_ = 0;
    uint32_t csr_ = 0x0E000000u;
    uint32_t sscgr_ = 0;
    uint32_t plli2scfgr_ = 0;
    ClockChanged onClock_;
};
