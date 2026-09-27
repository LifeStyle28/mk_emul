#pragma once

#include "periph/Peripheral.h"

#include <cstdint>
#include <functional>
#include <vector>

class MmioBus {
public:
    void add(Peripheral *p);
    uint32_t read(uint32_t addr, unsigned size);
    void write(uint32_t addr, uint32_t value, unsigned size);
    Peripheral *find(uint32_t addr) const;
    const std::vector<Peripheral *> &peripherals() const { return items_; }

    // beforeAccess runs before every read/write (to bring timers up to date), afterWrite after
    // every write (a new timer setting may move the next interrupt closer).
    void setAccessHooks(std::function<void()> beforeAccess, std::function<void()> afterWrite);

private:
    std::vector<Peripheral *> items_;
    std::function<void()> beforeAccess_;
    std::function<void()> afterWrite_;
};
