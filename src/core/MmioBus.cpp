#include "core/MmioBus.h"

#include <algorithm>

void MmioBus::add(Peripheral *p) {
    items_.push_back(p);
    std::sort(items_.begin(), items_.end(), [](Peripheral *a, Peripheral *b) {
        return a->size() < b->size();
    });
}

Peripheral *MmioBus::find(uint32_t addr) const {
    for (auto *p : items_) {
        if (p->covers(addr))
            return p;
    }
    return nullptr;
}

void MmioBus::setAccessHooks(std::function<void()> beforeAccess, std::function<void()> afterWrite) {
    beforeAccess_ = std::move(beforeAccess);
    afterWrite_ = std::move(afterWrite);
}

uint32_t MmioBus::read(uint32_t addr, unsigned size) {
    auto *p = find(addr);
    if (!p)
        return 0;
    if (beforeAccess_)
        beforeAccess_();
    const uint32_t word = p->read32(addr & ~3u);
    return extractSized(word, addr, size);
}

void MmioBus::write(uint32_t addr, uint32_t value, unsigned size) {
    auto *p = find(addr);
    if (!p)
        return;
    if (beforeAccess_)
        beforeAccess_();
    const uint32_t aligned = addr & ~3u;
    if (size >= 4 && (addr & 3u) == 0) {
        p->write32(aligned, value);
    } else {
        // Must not use read32(): reading e.g. USARTx->DR pops the RX FIFO.
        uint32_t word = p->partialWriteBase(aligned);
        word = insertSized(word, addr, size, value);
        p->write32(aligned, word);
    }
    if (afterWrite_)
        afterWrite_();
}
