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

void MmioBus::setMemoryAccess(std::function<uint32_t(uint32_t)> read32, std::function<void(uint32_t, uint32_t)> write32) {
    memRead32_ = std::move(read32);
    memWrite32_ = std::move(write32);
}

namespace {

bool isBitbandAlias(uint32_t addr) {
    return (addr & 0xFE000000u) == MmioBus::kPeriphBitbandBase || (addr & 0xFE000000u) == MmioBus::kSramBitbandBase;
}

// Word holding the aliased bit and the bit index in it.
void bitbandTarget(uint32_t alias, uint32_t *word, unsigned *bit) {
    const uint32_t offset = alias & 0x01FFFFFFu;
    const uint32_t byteAddr = (alias & 0xF0000000u) + (offset >> 5);
    *word = byteAddr & ~3u;
    *bit = (byteAddr & 3u) * 8u + ((offset >> 2) & 7u);
}

} // namespace

uint32_t MmioBus::bitbandRead(uint32_t alias) {
    uint32_t word;
    unsigned bit;
    bitbandTarget(alias, &word, &bit);
    uint32_t value = 0;
    if ((alias & 0xFE000000u) == kSramBitbandBase) {
        if (memRead32_)
            value = memRead32_(word);
    } else {
        value = read(word, 4);
    }
    return (value >> bit) & 1u;
}

void MmioBus::bitbandWrite(uint32_t alias, uint32_t value) {
    uint32_t word;
    unsigned bit;
    bitbandTarget(alias, &word, &bit);
    const uint32_t mask = 1u << bit;
    if ((alias & 0xFE000000u) == kSramBitbandBase) {
        if (memRead32_ && memWrite32_)
            memWrite32_(word, (memRead32_(word) & ~mask) | ((value & 1u) ? mask : 0u));
        return;
    }
    auto *p = find(word);
    if (!p) {
        if (unmapped_)
            unmapped_(word, true);
        return;
    }
    if (beforeAccess_)
        beforeAccess_();
    // Other bits as for a byte write (side-effect-free base), so e.g. a bit-band write to a
    // control register does not pop a data FIFO.
    const uint32_t base = p->partialWriteBase(word);
    p->write32(word, (base & ~mask) | ((value & 1u) ? mask : 0u));
    if (afterWrite_)
        afterWrite_();
}

uint32_t MmioBus::read(uint32_t addr, unsigned size) {
    if (isBitbandAlias(addr))
        return bitbandRead(addr & ~3u);
    auto *p = find(addr);
    if (!p) {
        if (unmapped_)
            unmapped_(addr, false);
        return 0;
    }
    if (beforeAccess_)
        beforeAccess_();
    const uint32_t word = p->read32(addr & ~3u);
    return extractSized(word, addr, size);
}

void MmioBus::write(uint32_t addr, uint32_t value, unsigned size) {
    if (isBitbandAlias(addr)) {
        bitbandWrite(addr & ~3u, value);
        return;
    }
    auto *p = find(addr);
    if (!p) {
        if (unmapped_)
            unmapped_(addr, true);
        return;
    }
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
