#pragma once

#include "periph/Peripheral.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

class Rcc;
class IrqTarget;

class Usart : public Peripheral {
public:
    using TxCallback = std::function<void(uint8_t byte)>;

    Usart(const char *name, uint32_t base, int irq, Rcc *rcc, IrqTarget *nvic, int usartIndex);

    const char *name() const override { return name_; }
    uint32_t base() const override { return base_; }
    uint32_t size() const override { return 0x400u; }

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void reset() override;

    void pushRx(uint8_t byte);
    void setTxCallback(TxCallback cb) { onTx_ = std::move(cb); }

    uint32_t sr() const { return sr_; }
    uint32_t cr1() const { return cr1_; }

private:
    void updateIrq();
    bool clocked() const;

    const char *name_;
    uint32_t base_;
    int irq_;
    int usartIndex_;
    Rcc *rcc_;
    IrqTarget *nvic_;
    uint32_t sr_ = 0x00C0u;
    uint32_t dr_ = 0;
    uint32_t brr_ = 0;
    uint32_t cr1_ = 0;
    uint32_t cr2_ = 0;
    uint32_t cr3_ = 0;
    uint32_t gtpr_ = 0;
    std::deque<uint8_t> rx_;
    TxCallback onTx_;
};
