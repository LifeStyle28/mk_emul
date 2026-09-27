#include "core/Nvic.h"

#include "core/Addresses.h"
#include "core/CpuEngine.h"

#include <unicorn/arm.h>

#include <algorithm>

Nvic::Nvic(CpuEngine *cpu)
    : cpu_(cpu)
    , systickPending_(0)
    , pendsvPending_(0)
    , nmiPending_(0) {
    reset();
}

bool Nvic::covers(uint32_t addr) const {
    if (addr >= 0xE000E010u && addr < 0xE000E020u)
        return false; // SysTick
    return addr >= 0xE000E000u && addr < 0xE000F000u;
}

void Nvic::reset() {
    iser_.fill(0);
    ispr_.fill(0);
    iabr_.fill(0);
    ipr_.fill(0);
    shpr_.fill(0);
    icsr_ = 0;
    vtor_ = 0;
    aircr_ = 0xFA050000u;
    scr_ = 0;
    ccr_ = 0x00000200u;
    cpacr_ = 0;
    if (cpu_)
        cpu_->setCpacr(0);
    shcsr_ = 0;
    cfsr_ = 0;
    hfsr_ = 0;
    mmfar_ = 0;
    bfar_ = 0;
    fpccr_ = 0xC0000000u; // ASPEN | LSPEN
    fpcar_ = 0;
    fpdscr_ = 0;
    currentException_ = 0;
    pendingEnabled_ = false;
    resetRequested_ = false;
    systickPending_ = 0;
    pendsvPending_ = 0;
    nmiPending_ = 0;
}

void Nvic::setPending(int irqNumber, bool pending) {
    if (irqNumber < 0 || irqNumber >= 240)
        return;
    setPendingBit(16 + irqNumber, pending);
}

void Nvic::setSystemPending(int exceptionNumber, bool pending) {
    setPendingBit(exceptionNumber, pending);
}

void Nvic::setPendingBit(int exceptionNumber, bool on) {
    if (exceptionNumber == 2)
        nmiPending_ = on ? 1 : 0;
    else if (exceptionNumber == 14)
        pendsvPending_ = on ? 1 : 0;
    else if (exceptionNumber == 15)
        systickPending_ = on ? 1 : 0;
    else if (exceptionNumber >= 16) {
        const int irq = exceptionNumber - 16;
        const int word = irq / 32;
        const int bit = irq % 32;
        if (on)
            ispr_[word] |= (1u << bit);
        else
            ispr_[word] &= ~(1u << bit);
    }
    updatePendingFlag();
}

bool Nvic::pending(int exceptionNumber) const {
    if (exceptionNumber == 2)
        return nmiPending_;
    if (exceptionNumber == 14)
        return pendsvPending_;
    if (exceptionNumber == 15)
        return systickPending_;
    if (exceptionNumber >= 16) {
        const int irq = exceptionNumber - 16;
        return (ispr_[irq / 32] >> (irq % 32)) & 1u;
    }
    return false;
}

bool Nvic::enabled(int exceptionNumber) const {
    if (exceptionNumber < 16)
        return true;
    const int irq = exceptionNumber - 16;
    return (iser_[irq / 32] >> (irq % 32)) & 1u;
}

int Nvic::priorityOf(int exceptionNumber) const {
    if (exceptionNumber == 1)
        return -3; // Reset
    if (exceptionNumber == 2)
        return -2; // NMI
    if (exceptionNumber == 3)
        return -1; // HardFault
    if (exceptionNumber < 16) {
        const int idx = exceptionNumber - 4;
        if (idx >= 0 && idx < 12)
            return shpr_[idx];
        return 0;
    }
    const int irq = exceptionNumber - 16;
    if (irq >= 0 && irq < 240)
        return ipr_[irq];
    return 0;
}

int Nvic::groupPriority(int priority) const {
    if (priority < 0)
        return priority;
    // PRIGROUP = n: bits [7:n+1] are the group (preemption) priority, the rest is subpriority.
    const uint32_t prigroup = (aircr_ >> 8) & 7u;
    const uint32_t mask = (0xFFu << (prigroup + 1)) & 0xFFu;
    return static_cast<int>(static_cast<uint32_t>(priority) & mask);
}

void Nvic::updatePendingFlag() {
    bool any = nmiPending_ || pendsvPending_ || systickPending_;
    for (size_t i = 0; !any && i < ispr_.size(); ++i)
        any = (ispr_[i] & iser_[i]) != 0;
    pendingEnabled_ = any;
}

int Nvic::executionPriority(CpuEngine &cpu) const {
    int pri = 256;
    if (currentException_ != 0)
        pri = groupPriority(priorityOf(currentException_));
    const int basepri = static_cast<int>(cpu.basepri() & 0xFFu);
    if (basepri != 0)
        pri = std::min(pri, groupPriority(basepri));
    if (cpu.primask() & 1u)
        pri = std::min(pri, 0); // only NMI and HardFault
    if (cpu.faultmask() & 1u)
        pri = std::min(pri, -1); // only NMI
    return pri;
}

bool Nvic::wouldPreempt(CpuEngine &cpu) const {
    const int cand = highestPending();
    return cand != 0 && groupPriority(priorityOf(cand)) < executionPriority(cpu);
}

int Nvic::highestPending() const {
    int best = 0;
    int bestPri = 256;
    auto consider = [&](int exc) {
        if (!pending(exc) || !enabled(exc))
            return;
        const int pri = priorityOf(exc);
        if (pri < bestPri || (pri == bestPri && (best == 0 || exc < best))) {
            bestPri = pri;
            best = exc;
        }
    };
    consider(2);
    consider(14);
    consider(15);
    for (int irq = 0; irq < 82; ++irq)
        consider(16 + irq);
    return best;
}

uint32_t Nvic::vectorAddress(int exceptionNumber) const {
    const uint32_t table = vtor_ ? vtor_ : mcu::kFlashAlias;
    return cpu_->read32(table + static_cast<uint32_t>(exceptionNumber) * 4u);
}

// Unicorn keeps the active stack pointer in SP and the other one aside; which is which follows
// its own view of the mode (handler mode or SPSEL = 0 means MSP). It ignores SPSEL writes in
// handler mode, and MSP/PSP/CONTROL writes from unprivileged thread code. So entry and exit
// switch SPSEL while in thread mode, run with nPRIV cleared, then set MSP and PSP explicitly.
void Nvic::enterException(CpuEngine &cpu, int exceptionNumber) {
    const uint32_t xpsr = cpu.xpsr();
    const uint32_t control = cpu.control();
    const uint32_t ipsr = xpsr & 0x1FFu;
    const bool handlerMode = ipsr != 0;
    const bool spSel = (control & 2u) != 0;
    const bool usePsp = !handlerMode && spSel;
    const bool nPriv = (control & 1u) != 0;
    // CONTROL.FPCA: the interrupted code has an active FP context (Unicorn sets it on the first
    // FP instruction). Then the extended frame also holds S0-S15 and FPSCR.
    const bool fpFrame = (control & kControlFpca) != 0;

    cpu.setNPriv(false);
    uint32_t msp = cpu.msp();
    uint32_t psp = cpu.psp();
    uint32_t &sp = usePsp ? psp : msp;

    uint32_t frame = sp;
    const uint32_t align = frame & 4u;
    frame &= ~4u;
    frame -= fpFrame ? kFpFrameSize : kBasicFrameSize;
    sp = frame;

    uint32_t r[16];
    cpu.snapshotRegs(r);

    cpu.write32(frame + 0, r[0]);
    cpu.write32(frame + 4, r[1]);
    cpu.write32(frame + 8, r[2]);
    cpu.write32(frame + 12, r[3]);
    cpu.write32(frame + 16, r[12]);
    cpu.write32(frame + 20, r[14]);
    cpu.write32(frame + 24, r[15]);
    uint32_t stackedXpsr = xpsr;
    if (align)
        stackedXpsr |= (1u << 9);
    else
        stackedXpsr &= ~(1u << 9);
    cpu.write32(frame + 28, stackedXpsr);
    if (fpFrame) {
        // Saved eagerly; with lazy stacking (FPCCR.LSPEN) the hardware only reserves the space
        // and fills it on the handler's first FP instruction - the same memory in the end.
        for (int i = 0; i < 16; ++i)
            cpu.write32(frame + 0x20u + 4u * i, cpu.sreg(i));
        cpu.write32(frame + 0x60u, cpu.fpscr());
        fpcar_ = frame + 0x20u;
    }

    // Still in the interrupted mode: the handler runs on MSP (SPSEL = 0) without an FP context.
    cpu.setControl(control & ~(1u | 2u | kControlFpca));
    cpu.setMsp(msp);
    cpu.setPsp(psp);

    uint32_t excReturn = 0xFFFFFFF9u;
    if (handlerMode)
        excReturn = 0xFFFFFFF1u;
    else if (spSel)
        excReturn = 0xFFFFFFFDu;
    if (fpFrame)
        excReturn &= ~0x10u; // 0xFFFFFFE1 / E9 / ED
    cpu.setLr(excReturn);

    const uint32_t handler = vectorAddress(exceptionNumber);
    cpu.setPc(handler & ~1u);

    const uint32_t newXpsr = (xpsr & ~0x1FFu) | (static_cast<uint32_t>(exceptionNumber) & 0x1FFu) | (1u << 24);
    cpu.setXpsr(newXpsr);
    if (nPriv)
        cpu.setNPriv(true); // handler mode is privileged anyway; applies again after return

    currentException_ = exceptionNumber;
    setPendingBit(exceptionNumber, false);
    if (exceptionNumber >= 16) {
        const int irq = exceptionNumber - 16;
        iabr_[irq / 32] |= 1u << (irq % 32);
    }
}

void Nvic::exitException(CpuEngine &cpu, uint32_t excReturn) {
    const uint32_t mode = excReturn & 0xFu;
    const bool toPsp = mode == 0xDu;
    const bool fpFrame = (excReturn & 0x10u) == 0;
    const uint32_t control = cpu.control();
    const bool nPriv = (control & 1u) != 0;

    uint32_t msp = cpu.msp(); // handler mode: privileged, SPSEL = 0
    uint32_t psp = cpu.psp();
    uint32_t &sp = toPsp ? psp : msp;

    const uint32_t frame = sp;
    const uint32_t r0 = cpu.read32(frame + 0);
    const uint32_t r1 = cpu.read32(frame + 4);
    const uint32_t r2 = cpu.read32(frame + 8);
    const uint32_t r3 = cpu.read32(frame + 12);
    const uint32_t r12 = cpu.read32(frame + 16);
    const uint32_t lr = cpu.read32(frame + 20);
    const uint32_t pc = cpu.read32(frame + 24);
    const uint32_t xpsr = cpu.read32(frame + 28);

    if (fpFrame) {
        for (int i = 0; i < 16; ++i)
            cpu.setSreg(i, cpu.read32(frame + 0x20u + 4u * i));
        cpu.setFpscr(cpu.read32(frame + 0x60u));
    }
    sp = frame + (fpFrame ? kFpFrameSize : kBasicFrameSize) + ((xpsr & (1u << 9)) ? 4u : 0u);

    // Still in handler mode: FPCA comes from EXC_RETURN; nPRIV is cleared until the stack
    // pointers are set, otherwise Unicorn would ignore those writes in unprivileged thread mode.
    cpu.setControl((fpFrame ? control | kControlFpca : control & ~kControlFpca) & ~1u);

    cpu.setReg(UC_ARM_REG_R0, r0);
    cpu.setReg(UC_ARM_REG_R1, r1);
    cpu.setReg(UC_ARM_REG_R2, r2);
    cpu.setReg(UC_ARM_REG_R3, r3);
    cpu.setReg(UC_ARM_REG_R12, r12);
    cpu.setLr(lr);
    cpu.setPc(pc & ~1u);
    cpu.setXpsr(xpsr | (1u << 24));

    if (toPsp)
        cpu.setControl(cpu.control() | 2u); // thread mode now: PSP becomes the active SP
    cpu.setMsp(msp);
    cpu.setPsp(psp);
    if (nPriv)
        cpu.setNPriv(true);

    currentException_ = static_cast<int>(xpsr & 0x1FFu);
    if (currentException_ >= 16) {
        const int irq = currentException_ - 16;
        if (irq >= 0 && irq < 240)
            iabr_[irq / 32] &= ~(1u << (irq % 32));
    }
}

int Nvic::raiseFault(CpuEngine &cpu, FaultType type, uint32_t address) {
    int exc = 3; // HardFault
    uint32_t enableBit = 0;
    switch (type) {
    case FaultType::BusData:
        cfsr_ |= (1u << 9) | (1u << 15); // PRECISERR | BFARVALID
        bfar_ = address;
        exc = 5;
        enableBit = 1u << 17; // BUSFAULTENA
        break;
    case FaultType::BusFetch:
        cfsr_ |= 1u << 8; // IBUSERR
        exc = 5;
        enableBit = 1u << 17;
        break;
    case FaultType::UndefInstr:
        cfsr_ |= 1u << 16; // UNDEFINSTR
        exc = 6;
        enableBit = 1u << 18; // USGFAULTENA
        break;
    case FaultType::Unaligned:
        cfsr_ |= 1u << 24; // UNALIGNED
        exc = 6;
        enableBit = 1u << 18;
        break;
    case FaultType::NoCoprocessor:
        cfsr_ |= 1u << 19; // NOCP: FP instruction with the FPU disabled in CPACR
        exc = 6;
        enableBit = 1u << 18;
        break;
    case FaultType::Other:
        break;
    }

    if (exc != 3) {
        // Configurable fault that is disabled, masked or not higher priority than the
        // current context escalates to HardFault.
        if (!(shcsr_ & enableBit) || groupPriority(priorityOf(exc)) >= executionPriority(cpu))
            exc = 3;
    }
    if (exc == 3) {
        if (currentException_ == 2 || currentException_ == 3)
            return 0; // fault in HardFault/NMI: lockup
        if (type != FaultType::Other)
            hfsr_ |= 1u << 30; // FORCED
    }

    const uint32_t handler = vectorAddress(exc);
    if (handler == 0 || handler == 0xFFFFFFFFu)
        return 0;
    enterException(cpu, exc);
    return exc;
}

int Nvic::raiseSvc(CpuEngine &cpu) {
    int exc = 11; // SVCall
    if (groupPriority(priorityOf(exc)) >= executionPriority(cpu)) {
        // SVC with SVCall masked or not higher priority than the current context -> HardFault.
        if (currentException_ == 2 || currentException_ == 3)
            return 0;
        hfsr_ |= 1u << 30; // FORCED
        exc = 3;
    }
    const uint32_t handler = vectorAddress(exc);
    if (handler == 0 || handler == 0xFFFFFFFFu)
        return 0;
    enterException(cpu, exc);
    return exc;
}

bool Nvic::takeResetRequest() {
    const bool r = resetRequested_;
    resetRequested_ = false;
    return r;
}

void Nvic::service(CpuEngine &cpu) {
    if (wouldPreempt(cpu))
        enterException(cpu, highestPending());
}

uint32_t Nvic::debugIcsr() const {
    uint32_t v = static_cast<uint32_t>(currentException_) & 0x1FFu;
    if (systickPending_)
        v |= 1u << 26;
    if (pendsvPending_)
        v |= 1u << 28;
    if (nmiPending_)
        v |= 1u << 31;
    return v;
}

uint32_t Nvic::read32(uint32_t addr) {
    if (addr == 0xE000E000u)
        return 0x0000001Fu; // ICTR
    if (addr == 0xE000ED00u)
        return cpuId_;
    if (addr == 0xE000ED04u)
        return debugIcsr();
    if (addr == 0xE000ED08u)
        return vtor_;
    if (addr == 0xE000ED0Cu)
        return aircr_;
    if (addr == 0xE000ED10u)
        return scr_;
    if (addr == 0xE000ED14u)
        return ccr_;
    if (addr == 0xE000ED88u)
        return cpacr_;
    if (addr == 0xE000ED24u)
        return shcsr_;
    if (addr == 0xE000ED28u)
        return cfsr_;
    if (addr == 0xE000ED2Cu)
        return hfsr_;
    if (addr == 0xE000ED34u)
        return mmfar_;
    if (addr == 0xE000ED38u)
        return bfar_;
    if (addr == 0xE000EF34u)
        return fpccr_;
    if (addr == 0xE000EF38u)
        return fpcar_;
    if (addr == 0xE000EF3Cu)
        return fpdscr_;
    if (addr == 0xE000EF40u)
        return 0x10110021u; // MVFR0: single precision, FP registers, sqrt, divide
    if (addr == 0xE000EF44u)
        return 0x11000011u; // MVFR1: FPv4 with FTZ/DN, half precision, fused MAC
    if (addr >= 0xE000ED18u && addr < 0xE000ED24u) {
        const uint32_t off = addr - 0xE000ED18u;
        uint32_t w = 0;
        for (int i = 0; i < 4; ++i)
            w |= static_cast<uint32_t>(shpr_[off + i]) << (8 * i);
        return w;
    }
    if (addr >= 0xE000E100u && addr < 0xE000E120u)
        return iser_[(addr - 0xE000E100u) / 4];
    if (addr >= 0xE000E180u && addr < 0xE000E1A0u)
        return iser_[(addr - 0xE000E180u) / 4]; // ICER reads as enable
    if (addr >= 0xE000E200u && addr < 0xE000E220u)
        return ispr_[(addr - 0xE000E200u) / 4];
    if (addr >= 0xE000E280u && addr < 0xE000E2A0u)
        return ispr_[(addr - 0xE000E280u) / 4];
    if (addr >= 0xE000E300u && addr < 0xE000E320u)
        return iabr_[(addr - 0xE000E300u) / 4];
    if (addr >= 0xE000E400u && addr < 0xE000E4F0u) {
        const uint32_t off = addr - 0xE000E400u;
        uint32_t w = 0;
        for (int i = 0; i < 4; ++i)
            w |= static_cast<uint32_t>(ipr_[off + i]) << (8 * i);
        return w;
    }
    return 0;
}

uint32_t Nvic::partialWriteBase(uint32_t addr) {
    // Set/clear-enable and set/clear-pending are write-1 registers, ICSR has trigger bits,
    // CFSR/HFSR are rc_w1: untouched bytes must be 0 so they have no effect.
    if (addr == 0xE000ED04u || addr == 0xE000ED28u || addr == 0xE000ED2Cu)
        return 0;
    if (addr >= 0xE000E100u && addr < 0xE000E2A0u)
        return 0;
    return read32(addr);
}

void Nvic::write32(uint32_t addr, uint32_t value) {
    if (addr == 0xE000ED04u) {
        if (value & (1u << 25))
            systickPending_ = 0;
        if (value & (1u << 26))
            systickPending_ = 1;
        if (value & (1u << 27))
            pendsvPending_ = 0;
        if (value & (1u << 28))
            pendsvPending_ = 1;
        if (value & (1u << 31))
            nmiPending_ = 1;
        updatePendingFlag();
        return;
    }
    if (addr == 0xE000ED08u) {
        vtor_ = value & 0xFFFFFF80u;
        return;
    }
    if (addr == 0xE000ED0Cu) {
        if ((value >> 16) != 0x05FAu)
            return; // writes without VECTKEY are ignored
        aircr_ = 0xFA050000u | (value & 0x700u); // PRIGROUP
        if (value & 0x4u) {                    // SYSRESETREQ (NVIC_SystemReset)
            resetRequested_ = true;
            if (cpu_)
                cpu_->stop();
        }
        return;
    }
    if (addr == 0xE000ED10u) {
        scr_ = value;
        return;
    }
    if (addr == 0xE000ED14u) {
        ccr_ = value;
        return;
    }
    if (addr == 0xE000ED88u) {
        cpacr_ = value & 0x00F00000u; // only CP10/CP11 (the FPU) exist
        if (cpu_)
            cpu_->setCpacr(cpacr_);
        return;
    }
    if (addr == 0xE000ED24u) {
        shcsr_ = value & 0x0007FFFFu;
        return;
    }
    if (addr == 0xE000ED28u) {
        cfsr_ &= ~value; // rc_w1
        return;
    }
    if (addr == 0xE000ED2Cu) {
        hfsr_ &= ~value; // rc_w1
        return;
    }
    if (addr == 0xE000ED34u) {
        mmfar_ = value;
        return;
    }
    if (addr == 0xE000ED38u) {
        bfar_ = value;
        return;
    }
    if (addr == 0xE000EF34u) {
        fpccr_ = value & 0xC00001FBu;
        return;
    }
    if (addr == 0xE000EF38u) {
        fpcar_ = value & ~7u;
        return;
    }
    if (addr == 0xE000EF3Cu) {
        fpdscr_ = value & 0x07C00000u; // AHP, DN, FZ, RMode
        return;
    }
    if (addr >= 0xE000ED18u && addr < 0xE000ED24u) {
        const uint32_t off = addr - 0xE000ED18u;
        for (int i = 0; i < 4; ++i)
            shpr_[off + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xF0u); // 4 priority bits
        return;
    }
    if (addr >= 0xE000E100u && addr < 0xE000E120u)
        iser_[(addr - 0xE000E100u) / 4] |= value;
    else if (addr >= 0xE000E180u && addr < 0xE000E1A0u)
        iser_[(addr - 0xE000E180u) / 4] &= ~value;
    else if (addr >= 0xE000E200u && addr < 0xE000E220u)
        ispr_[(addr - 0xE000E200u) / 4] |= value;
    else if (addr >= 0xE000E280u && addr < 0xE000E2A0u)
        ispr_[(addr - 0xE000E280u) / 4] &= ~value;
    else if (addr >= 0xE000E400u && addr < 0xE000E4F0u) {
        const uint32_t off = addr - 0xE000E400u;
        for (int i = 0; i < 4; ++i)
            ipr_[off + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xF0u); // 4 priority bits
    }
    updatePendingFlag(); // ISER/ICER/ISPR/ICPR may have changed
}
