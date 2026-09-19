#pragma once

#include "periph/Peripheral.h"

#include <cstdint>
#include <vector>

class MmioBus {
public:
    void add(Peripheral *p);
    uint32_t read(uint32_t addr, unsigned size);
    void write(uint32_t addr, uint32_t value, unsigned size);
    Peripheral *find(uint32_t addr) const;
    const std::vector<Peripheral *> &peripherals() const { return items_; }

private:
    std::vector<Peripheral *> items_;
};
