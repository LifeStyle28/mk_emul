#pragma once

#include "periph/Peripheral.h"

#include <array>
#include <cstdint>

class CpuEngine;

class Nvic : public Peripheral, public IrqTarget {
public:
    explicit Nvic(CpuEngine *cpu);

    const char *name() const override { return "NVIC/SCB"; }
    uint32_t base() const override { return 0xE000E000u; }
    uint32_t size() const override { return 0x1000u; }
    bool covers(uint32_t addr) const override;

    uint32_t read32(uint32_t addr) override;
    void write32(uint32_t addr, uint32_t value) override;
    void reset() override;

    void setPending(int irqNumber, bool pending) override;
    void setSystemPending(int exceptionNumber, bool pending) override;

    void service(CpuEngine &cpu);
    void enterException(CpuEngine &cpu, int exceptionNumber);
    void exitException(CpuEngine &cpu, uint32_t excReturn);

    uint32_t vtor() const { return vtor_; }
    int currentException() const { return currentException_; }

    uint32_t debugIcsr() const;
    uint32_t debugVtor() const { return vtor_; }
    uint32_t debugCpacr() const { return cpacr_; }

private:
    int highestPending() const;
    int priorityOf(int exceptionNumber) const;
    bool enabled(int exceptionNumber) const;
    bool pending(int exceptionNumber) const;
    void setPendingBit(int exceptionNumber, bool on);
    uint32_t vectorAddress(int exceptionNumber) const;

    CpuEngine *cpu_;
    std::array<uint32_t, 8> iser_{};
    std::array<uint32_t, 8> ispr_{};
    std::array<uint32_t, 8> iabr_{};
    std::array<uint8_t, 240> ipr_{};
    std::array<uint8_t, 12> shpr_{};
    uint32_t icsr_ = 0;
    uint32_t vtor_ = 0;
    uint32_t aircr_ = 0xFA050000u;
    uint32_t scr_ = 0;
    uint32_t ccr_ = 0x00000200u;
    uint32_t cpacr_ = 0;
    uint32_t cpuId_ = 0x410FC241u; // Cortex-M4 r0p1
    int currentException_ = 0;
    uint32_t systickPending_ : 1;
    uint32_t pendsvPending_ : 1;
    uint32_t nmiPending_ : 1;
};
