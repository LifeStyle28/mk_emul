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
    uint32_t partialWriteBase(uint32_t addr) override;
    void reset() override;

    void setPending(int irqNumber, bool pending) override;
    void setSystemPending(int exceptionNumber, bool pending) override;

    void service(CpuEngine &cpu);
    void enterException(CpuEngine &cpu, int exceptionNumber);
    void exitException(CpuEngine &cpu, uint32_t excReturn);

    enum class FaultType { BusData, BusFetch, UndefInstr, Unaligned, Other };
    // Enters BusFault/UsageFault (if enabled in SHCSR and allowed by priority) or escalates
    // to HardFault, filling CFSR/HFSR/BFAR. Returns the exception number entered,
    // or 0 on lockup (fault inside HardFault/NMI, or no handler in the vector table).
    int raiseFault(CpuEngine &cpu, FaultType type, uint32_t address);

    uint32_t cfsr() const { return cfsr_; }
    uint32_t hfsr() const { return hfsr_; }
    uint32_t bfar() const { return bfar_; }

    // Execution priority of the running code (current exception, BASEPRI, PRIMASK, FAULTMASK);
    // lower is more urgent, 256 = thread mode with nothing masked.
    int executionPriority(CpuEngine &cpu) const;
    // True if the highest pending enabled exception would preempt the running code now.
    bool wouldPreempt(CpuEngine &cpu) const;
    // Cheap flag for the CPU's per-instruction hook: some enabled exception is pending.
    const bool *pendingFlag() const { return &pendingEnabled_; }
    // SVC instruction executed: enters SVCall, or HardFault if SVCall cannot be taken now.
    // Returns the exception entered, 0 on lockup.
    int raiseSvc(CpuEngine &cpu);
    // SYSRESETREQ was written to AIRCR (NVIC_SystemReset); returns and clears the request.
    bool takeResetRequest();

    uint32_t vtor() const { return vtor_; }
    int currentException() const { return currentException_; }

    uint32_t debugIcsr() const;
    uint32_t debugVtor() const { return vtor_; }
    uint32_t debugCpacr() const { return cpacr_; }

private:
    static constexpr uint32_t kControlFpca = 1u << 2;
    static constexpr uint32_t kBasicFrameSize = 0x20u; // R0-R3, R12, LR, PC, xPSR
    static constexpr uint32_t kFpFrameSize = 0x68u;    // + S0-S15, FPSCR, reserved word

    int highestPending() const;
    int priorityOf(int exceptionNumber) const;
    int groupPriority(int priority) const; // preemption part per AIRCR.PRIGROUP
    void updatePendingFlag();
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
    uint32_t shcsr_ = 0;
    uint32_t cfsr_ = 0;
    uint32_t hfsr_ = 0;
    uint32_t mmfar_ = 0;
    uint32_t bfar_ = 0;
    uint32_t fpccr_ = 0xC0000000u;
    uint32_t fpcar_ = 0;
    uint32_t fpdscr_ = 0;
    uint32_t cpuId_ = 0x410FC241u; // Cortex-M4 r0p1
    int currentException_ = 0;
    bool pendingEnabled_ = false;
    bool resetRequested_ = false;
    uint32_t systickPending_ : 1;
    uint32_t pendsvPending_ : 1;
    uint32_t nmiPending_ : 1;
};
