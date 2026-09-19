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
    currentException_ = 0;
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
    if (exceptionNumber < 4)
        return -3;
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

void Nvic::enterException(CpuEngine &cpu, int exceptionNumber) {
    const uint32_t xpsr = cpu.xpsr();
    const uint32_t control = cpu.control();
    const uint32_t ipsr = xpsr & 0x1FFu;
    const bool handlerMode = ipsr != 0;
    const bool spSel = (control & 2u) != 0;
    const bool usePsp = !handlerMode && spSel;

    uint32_t frame = usePsp ? cpu.psp() : cpu.msp();
    const uint32_t align = frame & 4u;
    frame &= ~4u;
    frame -= 0x20u;

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

    if (usePsp)
        cpu.setPsp(frame);
    else
        cpu.setMsp(frame);

    uint32_t excReturn = 0xFFFFFFF9u;
    if (handlerMode)
        excReturn = 0xFFFFFFF1u;
    else if (spSel)
        excReturn = 0xFFFFFFFDu;
    cpu.setLr(excReturn);

    const uint32_t handler = vectorAddress(exceptionNumber);
    cpu.setPc(handler & ~1u);

    const uint32_t newXpsr = (xpsr & ~0x1FFu) | (static_cast<uint32_t>(exceptionNumber) & 0x1FFu) | (1u << 24);
    cpu.setXpsr(newXpsr);
    cpu.setControl(control & ~2u);
    cpu.setSp(cpu.msp());

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
    const bool toHandler = mode == 0x1u;

    uint32_t frame = toPsp ? cpu.psp() : cpu.msp();
    const uint32_t r0 = cpu.read32(frame + 0);
    const uint32_t r1 = cpu.read32(frame + 4);
    const uint32_t r2 = cpu.read32(frame + 8);
    const uint32_t r3 = cpu.read32(frame + 12);
    const uint32_t r12 = cpu.read32(frame + 16);
    const uint32_t lr = cpu.read32(frame + 20);
    const uint32_t pc = cpu.read32(frame + 24);
    const uint32_t xpsr = cpu.read32(frame + 28);

    cpu.setReg(UC_ARM_REG_R0, r0);
    cpu.setReg(UC_ARM_REG_R1, r1);
    cpu.setReg(UC_ARM_REG_R2, r2);
    cpu.setReg(UC_ARM_REG_R3, r3);
    cpu.setReg(UC_ARM_REG_R12, r12);
    cpu.setLr(lr);
    cpu.setPc(pc & ~1u);
    cpu.setXpsr(xpsr | (1u << 24));

    frame += 0x20u;
    if (xpsr & (1u << 9))
        frame += 4u;

    if (toPsp) {
        cpu.setPsp(frame);
        cpu.setControl(cpu.control() | 2u);
        cpu.setSp(frame);
    } else {
        cpu.setMsp(frame);
        if (!toHandler)
            cpu.setControl(cpu.control() & ~2u);
        cpu.setSp(frame);
    }

    currentException_ = static_cast<int>(xpsr & 0x1FFu);
    if (currentException_ >= 16) {
        const int irq = currentException_ - 16;
        if (irq >= 0 && irq < 240)
            iabr_[irq / 32] &= ~(1u << (irq % 32));
    }
}

void Nvic::service(CpuEngine &cpu) {
    if (cpu.primask() != 0)
        return;
    const int cand = highestPending();
    if (cand == 0)
        return;
    if (currentException_ != 0) {
        const int curPri = priorityOf(currentException_);
        const int newPri = priorityOf(cand);
        if (newPri >= curPri)
            return;
    }
    enterException(cpu, cand);
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
        return;
    }
    if (addr == 0xE000ED08u) {
        vtor_ = value & 0xFFFFFF80u;
        return;
    }
    if (addr == 0xE000ED0Cu) {
        aircr_ = (value & 0xFFFFu) | 0xFA050000u;
        if ((value & 0x04u) && ((value >> 16) == 0x05FAu)) {
            // SYSRESETREQ — Machine handles via halt? ignore, user clicks Reset
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
        cpacr_ = value;
        return;
    }
    if (addr >= 0xE000ED18u && addr < 0xE000ED24u) {
        const uint32_t off = addr - 0xE000ED18u;
        for (int i = 0; i < 4; ++i)
            shpr_[off + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
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
            ipr_[off + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xFFu);
    }
}
