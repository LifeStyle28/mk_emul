#include "periph/Rcc.h"

void Rcc::reset() {
    cr_ = 0x00000083u; // HSION | HSIRDY | HSITRIM=16
    pllcfgr_ = 0x24003010u;
    cfgr_ = 0;
    cir_ = 0;
    ahb1rstr_ = 0;
    ahb2rstr_ = 0;
    ahb3rstr_ = 0;
    apb1rstr_ = 0;
    apb2rstr_ = 0;
    ahb1enr_ = 0;
    ahb2enr_ = 0;
    ahb3enr_ = 0;
    apb1enr_ = 0;
    apb2enr_ = 0;
    ahb1lpenr_ = 0;
    ahb2lpenr_ = 0;
    ahb3lpenr_ = 0;
    apb1lpenr_ = 0;
    apb2lpenr_ = 0;
    bdcr_ = 0;
    csr_ = 0x0E000000u;
    sscgr_ = 0;
    plli2scfgr_ = 0x20003000u;
}

uint32_t Rcc::prescaleAhb(uint32_t hpre) {
    if (hpre < 8)
        return 1;
    static const uint32_t t[] = {2, 4, 8, 16, 64, 128, 256, 512};
    return t[hpre - 8];
}

uint32_t Rcc::prescaleApb(uint32_t ppre) {
    if (ppre < 4)
        return 1;
    return 1u << (ppre - 3);
}

uint32_t Rcc::sysclk() const {
    const uint32_t sws = (cfgr_ >> 2) & 3u;
    if (sws == 0)
        return mcu::kHsiHz;
    if (sws == 1)
        return mcu::kHseHz;
    const uint32_t src = (pllcfgr_ & (1u << 22)) ? mcu::kHseHz : mcu::kHsiHz;
    uint32_t m = pllcfgr_ & 0x3Fu;
    uint32_t n = (pllcfgr_ >> 6) & 0x1FFu;
    uint32_t p = (((pllcfgr_ >> 16) & 3u) + 1u) * 2u;
    if (m == 0)
        m = 2;
    if (n == 0)
        n = 1;
    if (p == 0)
        p = 2;
    return static_cast<uint32_t>((static_cast<uint64_t>(src) / m) * n / p);
}

uint32_t Rcc::hclk() const {
    return sysclk() / prescaleAhb((cfgr_ >> 4) & 0xFu);
}

uint32_t Rcc::pclk1() const {
    return hclk() / prescaleApb((cfgr_ >> 10) & 7u);
}

uint32_t Rcc::pclk2() const {
    return hclk() / prescaleApb((cfgr_ >> 13) & 7u);
}

uint32_t Rcc::timclk1() const {
    const uint32_t pre = prescaleApb((cfgr_ >> 10) & 7u);
    return pre == 1 ? pclk1() : pclk1() * 2;
}

uint32_t Rcc::timclk2() const {
    const uint32_t pre = prescaleApb((cfgr_ >> 13) & 7u);
    return pre == 1 ? pclk2() : pclk2() * 2;
}

bool Rcc::gpioEnabled(int portIndex) const {
    return (ahb1enr_ & (1u << portIndex)) != 0;
}

bool Rcc::usartEnabled(int index) const {
    switch (index) {
    case 1:
        return (apb2enr_ & (1u << 4)) != 0;
    case 2:
        return (apb1enr_ & (1u << 17)) != 0;
    case 3:
        return (apb1enr_ & (1u << 18)) != 0;
    case 6:
        return (apb2enr_ & (1u << 5)) != 0;
    default:
        return false;
    }
}

bool Rcc::timEnabled(int index) const {
    if (index == 2)
        return (apb1enr_ & (1u << 0)) != 0;
    if (index == 3)
        return (apb1enr_ & (1u << 1)) != 0;
    return false;
}

bool Rcc::adcEnabled() const { return (apb2enr_ & (1u << 8)) != 0; }
bool Rcc::spi1Enabled() const { return (apb2enr_ & (1u << 12)) != 0; }
bool Rcc::i2c1Enabled() const { return (apb1enr_ & (1u << 21)) != 0; }

uint32_t Rcc::read32(uint32_t addr) {
    switch (addr - mcu::kRccBase) {
    case 0x00:
        return cr_;
    case 0x04:
        return pllcfgr_;
    case 0x08:
        return cfgr_;
    case 0x0C:
        return cir_;
    case 0x10:
        return ahb1rstr_;
    case 0x14:
        return ahb2rstr_;
    case 0x18:
        return ahb3rstr_;
    case 0x20:
        return apb1rstr_;
    case 0x24:
        return apb2rstr_;
    case 0x30:
        return ahb1enr_;
    case 0x34:
        return ahb2enr_;
    case 0x38:
        return ahb3enr_;
    case 0x40:
        return apb1enr_;
    case 0x44:
        return apb2enr_;
    case 0x50:
        return ahb1lpenr_;
    case 0x54:
        return ahb2lpenr_;
    case 0x58:
        return ahb3lpenr_;
    case 0x60:
        return apb1lpenr_;
    case 0x64:
        return apb2lpenr_;
    case 0x70:
        return bdcr_;
    case 0x74:
        return csr_;
    case 0x80:
        return sscgr_;
    case 0x84:
        return plli2scfgr_;
    default:
        return 0;
    }
}

void Rcc::write32(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - mcu::kRccBase;
    switch (off) {
    case 0x00: {
        cr_ = value;
        if (cr_ & 1u)
            cr_ |= 2u; // HSIRDY
        else
            cr_ &= ~2u;
        if (cr_ & (1u << 16))
            cr_ |= (1u << 17); // HSERDY immediately
        else
            cr_ &= ~(1u << 17);
        if (cr_ & (1u << 24))
            cr_ |= (1u << 25); // PLLRDY
        else
            cr_ &= ~(1u << 25);
        if (cr_ & (1u << 26))
            cr_ |= (1u << 27); // PLLI2SRDY
        else
            cr_ &= ~(1u << 27);
        break;
    }
    case 0x04:
        pllcfgr_ = value;
        break;
    case 0x08: {
        cfgr_ = value;
        const uint32_t sw = cfgr_ & 3u;
        cfgr_ = (cfgr_ & ~(3u << 2)) | (sw << 2); // SWS = SW
        break;
    }
    case 0x0C:
        cir_ &= ~value; // write 1 to clear some bits; simplified
        break;
    case 0x10:
        ahb1rstr_ = value;
        break;
    case 0x14:
        ahb2rstr_ = value;
        break;
    case 0x18:
        ahb3rstr_ = value;
        break;
    case 0x20:
        apb1rstr_ = value;
        break;
    case 0x24:
        apb2rstr_ = value;
        break;
    case 0x30:
        ahb1enr_ = value;
        break;
    case 0x34:
        ahb2enr_ = value;
        break;
    case 0x38:
        ahb3enr_ = value;
        break;
    case 0x40:
        apb1enr_ = value;
        break;
    case 0x44:
        apb2enr_ = value;
        break;
    case 0x50:
        ahb1lpenr_ = value;
        break;
    case 0x54:
        ahb2lpenr_ = value;
        break;
    case 0x58:
        ahb3lpenr_ = value;
        break;
    case 0x60:
        apb1lpenr_ = value;
        break;
    case 0x64:
        apb2lpenr_ = value;
        break;
    case 0x70:
        bdcr_ = value;
        if (bdcr_ & 1u)
            bdcr_ |= 2u; // LSERDY
        break;
    case 0x74:
        csr_ = (csr_ & 0xFF000000u) | (value & 0x00FFFFFFu);
        if (csr_ & 1u)
            csr_ |= 2u; // LSIRDY
        break;
    case 0x80:
        sscgr_ = value;
        break;
    case 0x84:
        plli2scfgr_ = value;
        break;
    default:
        break;
    }
    if (onClock_)
        onClock_();
}
