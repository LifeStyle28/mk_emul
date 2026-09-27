#pragma once

#include "core/Machine.h"
#include "periph/Peripheral.h"

#include <cstdint>
#include <map>
#include <vector>

// Records what peripherals signal to the interrupt controller.
class FakeIrq : public IrqTarget {
public:
    void setPending(int irq, bool pending) override { irqs[irq] = pending; }
    void setSystemPending(int exc, bool pending) override { system[exc] = pending; }

    bool irq(int n) const {
        auto it = irqs.find(n);
        return it != irqs.end() && it->second;
    }
    bool sys(int n) const {
        auto it = system.find(n);
        return it != system.end() && it->second;
    }

    std::map<int, bool> irqs;
    std::map<int, bool> system;
};

// Builds a raw flash image (loaded at 0x08000000): vector table + hand-assembled Thumb code.
class FlashImage {
public:
    static constexpr uint32_t kBase = 0x08000000u;

    FlashImage() : bytes_(0x200, 0) {}

    FlashImage &word(uint32_t addr, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            at(addr + i) = static_cast<uint8_t>(v >> (8 * i));
        return *this;
    }
    // Consecutive 16-bit Thumb instructions starting at addr.
    FlashImage &thumb(uint32_t addr, std::initializer_list<uint16_t> insns) {
        for (uint16_t op : insns) {
            at(addr) = static_cast<uint8_t>(op);
            at(addr + 1) = static_cast<uint8_t>(op >> 8);
            addr += 2;
        }
        return *this;
    }
    FlashImage &code(uint32_t addr, const std::vector<uint16_t> &halfwords) {
        for (uint16_t op : halfwords) {
            at(addr) = static_cast<uint8_t>(op);
            at(addr + 1) = static_cast<uint8_t>(op >> 8);
            addr += 2;
        }
        return *this;
    }
    // Exception vector n points to Thumb code at addr.
    FlashImage &vector(int n, uint32_t addr) { return word(kBase + 4u * n, addr | 1u); }

    const std::vector<uint8_t> &bytes() const { return bytes_; }

private:
    uint8_t &at(uint32_t addr) { return bytes_.at(addr - kBase); }
    std::vector<uint8_t> bytes_;
};

inline void loadImage(Machine &m, const FlashImage &img) {
    m.loadFirmwareBuffer(img.bytes().data(), img.bytes().size(), "test.bin");
}

// Runs until `insns` instructions are executed or the CPU stops (breakpoint/fault).
inline CpuEngine::StopReason runFor(Machine &m, uint64_t insns) {
    const uint64_t target = m.cpu().instructionCount() + insns;
    auto stop = CpuEngine::StopReason::Quantum;
    while (m.cpu().instructionCount() < target) {
        const uint64_t before = m.cpu().instructionCount();
        const uint64_t left = target - before;
        stop = m.runQuantum(static_cast<uint32_t>(left > 12000 ? 12000 : left));
        if (stop == CpuEngine::StopReason::Breakpoint || stop == CpuEngine::StopReason::Fault)
            break;
        if (m.cpu().instructionCount() == before)
            break; // no progress: avoid hanging the test
    }
    return stop;
}

// Calls sample() after every quantum while running `insns` instructions.
template <typename F>
inline CpuEngine::StopReason runSampling(Machine &m, uint64_t insns, F sample) {
    const uint64_t target = m.cpu().instructionCount() + insns;
    auto stop = CpuEngine::StopReason::Quantum;
    while (m.cpu().instructionCount() < target) {
        const uint64_t before = m.cpu().instructionCount();
        stop = m.runQuantum(12000);
        sample();
        if (stop == CpuEngine::StopReason::Breakpoint || stop == CpuEngine::StopReason::Fault)
            break;
        if (m.cpu().instructionCount() == before)
            break;
    }
    return stop;
}
