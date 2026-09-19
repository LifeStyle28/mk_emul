#pragma once

#include <cstdint>
#include <functional>
#include <string>

class IrqTarget {
public:
    virtual ~IrqTarget() = default;
    virtual void setPending(int irqNumber, bool pending) = 0;
    virtual void setSystemPending(int exceptionNumber, bool pending) = 0;
};

class Peripheral {
public:
    virtual ~Peripheral() = default;
    virtual const char *name() const = 0;
    virtual uint32_t base() const = 0;
    virtual uint32_t size() const = 0;
    virtual bool covers(uint32_t addr) const {
        return addr >= base() && addr < base() + size();
    }
    virtual uint32_t read32(uint32_t addr) = 0;
    virtual void write32(uint32_t addr, uint32_t value) = 0;
    virtual void tick(uint32_t /*cycles*/, uint32_t /*sysclk*/) {}
    virtual void reset() = 0;
};

inline uint32_t extractSized(uint32_t word, uint32_t addr, unsigned size) {
    const uint32_t shift = (addr & 3u) * 8u;
    const uint32_t mask = size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : 0xFFFFFFFFu;
    return (word >> shift) & mask;
}

inline uint32_t insertSized(uint32_t word, uint32_t addr, unsigned size, uint32_t value) {
    const uint32_t shift = (addr & 3u) * 8u;
    const uint32_t mask = (size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : 0xFFFFFFFFu) << shift;
    return (word & ~mask) | ((value << shift) & mask);
}
