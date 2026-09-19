#include "periph/Usart.h"

#include "periph/Rcc.h"

Usart::Usart(const char *name, uint32_t base, int irq, Rcc *rcc, IrqTarget *nvic, int usartIndex)
    : name_(name)
    , base_(base)
    , irq_(irq)
    , usartIndex_(usartIndex)
    , rcc_(rcc)
    , nvic_(nvic) {
    reset();
}

void Usart::reset() {
    sr_ = 0x00C0u;
    dr_ = 0;
    brr_ = 0;
    cr1_ = 0;
    cr2_ = 0;
    cr3_ = 0;
    gtpr_ = 0;
    rx_.clear();
}

bool Usart::clocked() const {
    return !rcc_ || rcc_->usartEnabled(usartIndex_);
}

void Usart::updateIrq() {
    if (!nvic_)
        return;
    const bool rxneie = cr1_ & (1u << 5);
    const bool tcie = cr1_ & (1u << 6);
    const bool txeie = cr1_ & (1u << 7);
    const bool irq = ((sr_ & (1u << 5)) && rxneie) || ((sr_ & (1u << 6)) && tcie) || ((sr_ & (1u << 7)) && txeie);
    nvic_->setPending(irq_, irq);
}

void Usart::pushRx(uint8_t byte) {
    rx_.push_back(byte);
    sr_ |= (1u << 5); // RXNE
    updateIrq();
}

uint32_t Usart::read32(uint32_t addr) {
    switch (addr - base_) {
    case 0x00:
        return sr_;
    case 0x04: {
        uint32_t v = dr_;
        if (!rx_.empty()) {
            v = rx_.front();
            rx_.pop_front();
        }
        if (rx_.empty())
            sr_ &= ~(1u << 5);
        dr_ = v;
        updateIrq();
        return v & 0x1FFu;
    }
    case 0x08:
        return brr_;
    case 0x0C:
        return cr1_;
    case 0x10:
        return cr2_;
    case 0x14:
        return cr3_;
    case 0x18:
        return gtpr_;
    default:
        return 0;
    }
}

void Usart::write32(uint32_t addr, uint32_t value) {
    switch (addr - base_) {
    case 0x00:
        sr_ &= ~(value & 0x360u); // clearable bits
        break;
    case 0x04:
        dr_ = value & 0x1FFu;
        if (onTx_)
            onTx_(static_cast<uint8_t>(dr_));
        sr_ |= (1u << 6) | (1u << 7); // TC | TXE
        break;
    case 0x08:
        brr_ = value;
        break;
    case 0x0C:
        cr1_ = value;
        break;
    case 0x10:
        cr2_ = value;
        break;
    case 0x14:
        cr3_ = value;
        break;
    case 0x18:
        gtpr_ = value;
        break;
    default:
        break;
    }
    updateIrq();
    (void)clocked();
}
