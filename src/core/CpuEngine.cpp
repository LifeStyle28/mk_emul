#include "core/CpuEngine.h"

#include "core/MmioBus.h"

#include <unicorn/unicorn.h>
#include <unicorn/arm.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

struct MmioCookie {
    uint32_t base = 0;
    MmioBus *bus = nullptr;
};

uint64_t mmioRead(uc_engine *, uint64_t offset, unsigned size, void *user) {
    auto *c = static_cast<MmioCookie *>(user);
    return c->bus->read(c->base + static_cast<uint32_t>(offset), size);
}

void mmioWrite(uc_engine *, uint64_t offset, unsigned size, uint64_t value, void *user) {
    auto *c = static_cast<MmioCookie *>(user);
    c->bus->write(c->base + static_cast<uint32_t>(offset), static_cast<uint32_t>(value), size);
}

} // namespace

static std::vector<std::unique_ptr<MmioCookie>> g_cookies;

CpuEngine::CpuEngine() = default;

CpuEngine::~CpuEngine() {
    close();
}

bool CpuEngine::open(std::string *error) {
    close();
    uc_err err = uc_open(UC_ARCH_ARM, static_cast<uc_mode>(UC_MODE_THUMB | UC_MODE_LITTLE_ENDIAN), &uc_);
    if (err != UC_ERR_OK) {
        lastError_ = uc_strerror(err);
        if (error)
            *error = lastError_;
        uc_ = nullptr;
        return false;
    }

    // UC_CPU_ARM_CORTEX_M4 is an enum value, not a macro: an #ifdef around this call was
    // always false and Unicorn silently emulated a Cortex-A15 in Thumb mode. Don't pass
    // UC_MODE_MCLASS either: Unicorn then forces a Cortex-M33.
    err = uc_ctl_set_cpu_model(uc_, UC_CPU_ARM_CORTEX_M4);
    if (err != UC_ERR_OK) {
        lastError_ = std::string("Cortex-M4 model: ") + uc_strerror(err);
        if (error)
            *error = lastError_;
        uc_close(uc_);
        uc_ = nullptr;
        return false;
    }

    installHooks();
    return true;
}

void CpuEngine::close() {
    if (!uc_)
        return;
    uc_close(uc_);
    uc_ = nullptr;
    ram_.clear(); // after uc_close: Unicorn used these buffers
    codeHook_ = 0;
    intrHook_ = 0;
    memHook_ = 0;
}

void CpuEngine::mapRam(uint32_t addr, uint32_t size) {
    // Our own zeroed buffer, so the code hook can look at instructions without calling Unicorn.
    RamRegion r{addr, size, std::make_unique<uint8_t[]>(size)};
    if (uc_mem_map_ptr(uc_, addr, size, UC_PROT_ALL, r.data.get()) != UC_ERR_OK)
        return;
    ram_.push_back(std::move(r));
}

const uint8_t *CpuEngine::hostPtr(uint32_t addr, uint32_t len) const {
    for (const auto &r : ram_) {
        if (addr >= r.base && addr - r.base <= r.size - len)
            return r.data.get() + (addr - r.base);
    }
    return nullptr;
}

bool CpuEngine::isFpInstruction(uint32_t addr) const {
    const uint8_t *p = hostPtr(addr, 4);
    if (!p)
        return false;
    const uint32_t hw1 = p[0] | (p[1] << 8);
    const uint32_t hw2 = p[2] | (p[3] << 8);
    // Thumb-2 coprocessor space (111x 11xx ...) with coprocessor 10 or 11: every FPv4
    // instruction (VADD, VLDR/VSTR, VPUSH/VPOP, VMOV, VMRS/VMSR, ...).
    return (hw1 & 0xEC00u) == 0xEC00u && (hw2 & 0x0E00u) == 0x0A00u;
}

// Kept out of the per-instruction hook: a local char array there makes MSVC add a /GS stack
// cookie check to every instruction (measured: ~8% slower emulation).
MK_NOINLINE void CpuEngine::reportNoCoprocessor(uint32_t addr) {
    lastStop_ = StopReason::Fault;
    faultKind_ = FaultKind::NoCoprocessor;
    faultAddress_ = addr;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "FPU disabled (CPACR) @ 0x%08X", static_cast<unsigned>(addr));
    lastError_ = buf;
    if (onFault_)
        onFault_(lastError_, faultAddress_);
}

bool CpuEngine::fpAccessAllowed() const {
    if (fpAccess_ == 3)
        return true;
    if (fpAccess_ == 1)
        return privileged();
    return false;
}

void CpuEngine::mapMmio(uint32_t addr, uint32_t size, MmioBus *bus) {
    auto cookie = std::make_unique<MmioCookie>();
    cookie->base = addr;
    cookie->bus = bus;
    uc_err err = uc_mmio_map(uc_, addr, size, mmioRead, cookie.get(), mmioWrite, cookie.get());
    if (err != UC_ERR_OK) {
        lastError_ = uc_strerror(err);
        return;
    }
    g_cookies.push_back(std::move(cookie));
}

void CpuEngine::writeMem(uint32_t addr, const void *data, size_t size) {
    uc_mem_write(uc_, addr, data, size);
}

void CpuEngine::flushCodeCache() {
    // Unicorn caches translated blocks by address and does not drop them on uc_mem_write:
    // without this, code of the previous firmware keeps running at the same addresses.
    if (uc_)
        uc_ctl_flush_tb(uc_);
    // Find WFI instructions (Thumb 0xBF30) in flash. A data word that happens to contain it
    // matters only if code executes it, and then it is a WFI.
    constexpr uint32_t kFlash = 0x08000000u, kFlashSize = 1024u * 1024u;
    wfiMap_.assign(kFlashSize / 16, 0);
    hasWfi_ = false;
    if (const uint8_t *p = hostPtr(kFlash, kFlashSize)) {
        for (uint32_t off = 0; off < kFlashSize; off += 2) {
            if (p[off] == 0x30 && p[off + 1] == 0xBF) {
                wfiMap_[off >> 4] |= static_cast<uint8_t>(1u << ((off >> 1) & 7));
                hasWfi_ = true;
            }
        }
    }
}

void CpuEngine::readMem(uint32_t addr, void *data, size_t size) const {
    if (uc_mem_read(uc_, addr, data, size) == UC_ERR_OK)
        return;
    // The range touches unmapped memory (e.g. a disassembly window just below flash): read it
    // page by page so the mapped part is still returned; unmapped pages read as zero.
    constexpr uint64_t kPage = 0x1000;
    auto *out = static_cast<uint8_t *>(data);
    uint64_t a = addr;
    const uint64_t end = static_cast<uint64_t>(addr) + size;
    while (a < end) {
        const uint64_t chunkEnd = std::min(end, (a / kPage + 1) * kPage);
        const size_t n = static_cast<size_t>(chunkEnd - a);
        if (uc_mem_read(uc_, a, out + (a - addr), n) != UC_ERR_OK)
            std::memset(out + (a - addr), 0, n);
        a = chunkEnd;
    }
}

uint32_t CpuEngine::read32(uint32_t addr) const {
    uint32_t v = 0;
    readMem(addr, &v, 4);
    return v;
}

void CpuEngine::write32(uint32_t addr, uint32_t value) {
    writeMem(addr, &value, 4);
}

uint32_t CpuEngine::reg(int unicornReg) const {
    uint32_t v = 0;
    uc_reg_read(uc_, unicornReg, &v);
    return v;
}

void CpuEngine::setReg(int unicornReg, uint32_t value) {
    uc_reg_write(uc_, unicornReg, &value);
}

uint32_t CpuEngine::pc() const { return reg(UC_ARM_REG_PC) & ~1u; }
uint32_t CpuEngine::sp() const { return reg(UC_ARM_REG_SP); }
uint32_t CpuEngine::lr() const { return reg(UC_ARM_REG_LR); }
// Cortex-M: XPSR (not CPSR) carries IPSR, so Unicorn knows it is in handler mode and
// treats BX to EXC_RETURN as an exception return.
uint32_t CpuEngine::xpsr() const { return reg(UC_ARM_REG_XPSR); }
uint32_t CpuEngine::msp() const { return privilegedReg(UC_ARM_REG_MSP); }
uint32_t CpuEngine::psp() const { return privilegedReg(UC_ARM_REG_PSP); }
uint32_t CpuEngine::control() const { return reg(UC_ARM_REG_CONTROL); }
uint32_t CpuEngine::ipsr() const { return xpsr() & 0x1FFu; }
uint32_t CpuEngine::primask() const { return privilegedReg(UC_ARM_REG_PRIMASK); }
uint32_t CpuEngine::basepri() const { return privilegedReg(UC_ARM_REG_BASEPRI); }
uint32_t CpuEngine::faultmask() const { return privilegedReg(UC_ARM_REG_FAULTMASK); }

uint32_t CpuEngine::privilegedReg(int unicornReg) const {
    if (ipsr() != 0 || (control() & 1u) == 0)
        return reg(unicornReg);
    // Invisible to the program: nPRIV is restored right away.
    auto *self = const_cast<CpuEngine *>(this);
    self->setNPriv(false);
    const uint32_t v = reg(unicornReg);
    self->setNPriv(true);
    return v;
}

void CpuEngine::setNPriv(bool on) {
    const uint32_t c = control();
    if (((c & 1u) != 0) == on)
        return;
    if (!on && ipsr() == 0) {
        // Unprivileged thread code cannot clear nPRIV: do it from (briefly) handler mode, where
        // Unicorn also ignores SPSEL, so the active stack pointer does not change.
        const uint32_t x = xpsr();
        setXpsr((x & ~0x1FFu) | 3u);
        setControl(c & ~1u);
        setXpsr(x);
        return;
    }
    setControl(on ? c | 1u : c & ~1u);
}

void CpuEngine::setPc(uint32_t value) { setReg(UC_ARM_REG_PC, value); }
void CpuEngine::setSp(uint32_t value) { setReg(UC_ARM_REG_SP, value); }
void CpuEngine::setLr(uint32_t value) { setReg(UC_ARM_REG_LR, value); }
void CpuEngine::setXpsr(uint32_t value) {
    // NZCVQ + GE + IPSR + EPSR in one write.
    setReg(UC_ARM_REG_XPSR_NZCVQG, value);
    // Unicorn does not rebuild the cached hflags after an XPSR write, so the translator
    // would keep the old handler/thread mode. An APSR_NZCV write (same flags) rebuilds them.
    setReg(UC_ARM_REG_APSR_NZCV, value);
}
void CpuEngine::setMsp(uint32_t value) { setReg(UC_ARM_REG_MSP, value); }
void CpuEngine::setPsp(uint32_t value) { setReg(UC_ARM_REG_PSP, value); }
void CpuEngine::setControl(uint32_t value) {
    setReg(UC_ARM_REG_CONTROL, value);
    // CONTROL.FPCA decides whether the next FP instruction creates a new FP context; like XPSR,
    // it is part of the cached hflags, which only an APSR_NZCV write rebuilds.
    setReg(UC_ARM_REG_APSR_NZCV, xpsr());
}
void CpuEngine::setPrimask(uint32_t value) { setReg(UC_ARM_REG_PRIMASK, value); }
uint32_t CpuEngine::sreg(int n) const { return reg(UC_ARM_REG_S0 + n); }
void CpuEngine::setSreg(int n, uint32_t value) { setReg(UC_ARM_REG_S0 + n, value); }
uint32_t CpuEngine::fpscr() const { return reg(UC_ARM_REG_FPSCR); }
void CpuEngine::setFpscr(uint32_t value) { setReg(UC_ARM_REG_FPSCR, value); }

void CpuEngine::snapshotRegs(uint32_t out[16]) const {
    const int ids[16] = {
        UC_ARM_REG_R0,  UC_ARM_REG_R1,  UC_ARM_REG_R2,  UC_ARM_REG_R3,
        UC_ARM_REG_R4,  UC_ARM_REG_R5,  UC_ARM_REG_R6,  UC_ARM_REG_R7,
        UC_ARM_REG_R8,  UC_ARM_REG_R9,  UC_ARM_REG_R10, UC_ARM_REG_R11,
        UC_ARM_REG_R12, UC_ARM_REG_SP,  UC_ARM_REG_LR,  UC_ARM_REG_PC,
    };
    for (int i = 0; i < 16; ++i)
        out[i] = reg(ids[i]);
    out[15] &= ~1u;
}

void CpuEngine::installHooks() {
    auto *self = this;

    uc_hook_add(uc_, &intrHook_, UC_HOOK_INTR,
                (void *)+[](uc_engine *, uint32_t intno, void *user) {
                    auto *cpu = static_cast<CpuEngine *>(user);
                    if (cpu->onInterrupt_)
                        cpu->onInterrupt_(intno);
                    if (intno == 8)
                        cpu->lastStop_ = StopReason::InterruptHook;
                },
                self, 1, 0);

    uc_hook_add(uc_, &memHook_, UC_HOOK_MEM_UNMAPPED,
                (void *)+[](uc_engine *uc, uc_mem_type type, uint64_t addr, int, int64_t, void *user) -> bool {
                    auto *cpu = static_cast<CpuEngine *>(user);
                    cpu->faultAddress_ = static_cast<uint32_t>(addr);
                    cpu->lastStop_ = StopReason::Fault;
                    const char *kind = "memory";
                    cpu->faultKind_ = FaultKind::Other;
                    if (type == UC_MEM_READ_UNMAPPED) {
                        kind = "read unmapped";
                        cpu->faultKind_ = FaultKind::ReadUnmapped;
                    } else if (type == UC_MEM_WRITE_UNMAPPED) {
                        kind = "write unmapped";
                        cpu->faultKind_ = FaultKind::WriteUnmapped;
                    } else if (type == UC_MEM_FETCH_UNMAPPED) {
                        kind = "fetch unmapped";
                        cpu->faultKind_ = FaultKind::FetchUnmapped;
                    }
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s @ 0x%08X", kind, static_cast<unsigned>(addr));
                    cpu->lastError_ = buf;
                    if (cpu->onFault_)
                        cpu->onFault_(cpu->lastError_, cpu->faultAddress_);
                    uc_emu_stop(uc);
                    return false;
                },
                self, 1, 0);

    // One hook for every instruction: enforces the run limit, checks breakpoints and counts
    // instructions that actually executed. uc_emu_stop() from a code hook stops *before* the
    // current instruction, so it is not counted.
    uc_hook_add(uc_, &codeHook_, UC_HOOK_CODE,
                (void *)+[](uc_engine *uc, uint64_t address, uint32_t size, void *user) {
                    auto *cpu = static_cast<CpuEngine *>(user);
                    if (cpu->runExecuted_ >= cpu->runLimit_) {
                        uc_emu_stop(uc);
                        return;
                    }
                    // An enabled interrupt became pending (MMIO write, CPSIE, BASEPRI, return from
                    // another handler): stop before this instruction if it can preempt now.
                    if (cpu->irqPendingFlag_ && *cpu->irqPendingFlag_ && cpu->irqShouldStop_ &&
                        cpu->irqShouldStop_()) {
                        uc_emu_stop(uc);
                        return;
                    }
                    if (!cpu->breakpoints_.empty() &&
                        cpu->breakpoints_.count(static_cast<uint32_t>(address) & ~1u)) {
                        if (!cpu->skipNextBreakpoint_) {
                            cpu->lastStop_ = StopReason::Breakpoint;
                            uc_emu_stop(uc);
                            return;
                        }
                    }
                    cpu->skipNextBreakpoint_ = false;
                    // FPU not enabled in CPACR: NOCP UsageFault before the FP instruction runs.
                    if (cpu->fpAccess_ != 3 && size == 4 && cpu->isFpInstruction(static_cast<uint32_t>(address)) &&
                        !cpu->fpAccessAllowed()) {
                        cpu->reportNoCoprocessor(static_cast<uint32_t>(address));
                        uc_emu_stop(uc);
                        return;
                    }
                    // WFI: sleep unless an interrupt is already pending (then it is a NOP).
                    if (size == 2 && cpu->hasWfi_) {
                        const uint32_t off = static_cast<uint32_t>(address) - 0x08000000u;
                        if (off < cpu->wfiMap_.size() * 16 && ((cpu->wfiMap_[off >> 4] >> ((off >> 1) & 7)) & 1) &&
                            !(cpu->irqPendingFlag_ && *cpu->irqPendingFlag_)) {
                            cpu->wfiAt_ = static_cast<uint32_t>(address);
                            cpu->lastStop_ = StopReason::Sleep;
                            uc_emu_stop(uc);
                            return;
                        }
                    }
                    cpu->currentInsn_ = static_cast<uint32_t>(address);
                    ++cpu->runExecuted_;
                },
                self, 1, 0);
}

void CpuEngine::addBreakpoint(uint32_t addr) {
    breakpoints_.insert(addr & ~1u);
}

void CpuEngine::removeBreakpoint(uint32_t addr) {
    breakpoints_.erase(addr & ~1u);
}

void CpuEngine::clearBreakpoints() {
    breakpoints_.clear();
}

bool CpuEngine::hasBreakpoint(uint32_t addr) const {
    return breakpoints_.count(addr & ~1u) != 0;
}

std::vector<uint32_t> CpuEngine::breakpoints() const {
    std::vector<uint32_t> v(breakpoints_.begin(), breakpoints_.end());
    std::sort(v.begin(), v.end());
    return v;
}

void CpuEngine::setInterruptPoll(const bool *pendingFlag, std::function<bool()> shouldStop) {
    irqPendingFlag_ = pendingFlag;
    irqShouldStop_ = std::move(shouldStop);
}

void CpuEngine::setInterruptHandler(InterruptHandler handler) { onInterrupt_ = std::move(handler); }
void CpuEngine::setFaultHandler(FaultHandler handler) { onFault_ = std::move(handler); }

CpuEngine::StopReason CpuEngine::run(uint64_t maxInsns) {
    if (!uc_)
        return StopReason::Fault;
    lastStop_ = StopReason::Quantum;
    lastError_.clear();
    faultKind_ = FaultKind::None;
    runLimit_ = maxInsns;
    runExecuted_ = 0;
    const uint32_t pcNow = pc();
    skipNextBreakpoint_ = hasBreakpoint(pcNow);
    const uint64_t start = static_cast<uint64_t>(pcNow) | 1ull;
    // count = 0: the code hook above enforces maxInsns and keeps the exact count.
    rewindToCurrent_ = false;
    const uc_err err = uc_emu_start(uc_, start, 0, 0, 0);
    if (lastStop_ == StopReason::Sleep) {
        setPc(wfiAt_ + 2); // the WFI itself completes; the core sleeps after it
        ++runExecuted_;
    }
    if (rewindToCurrent_) {
        // Precise fault: the faulting instruction did not complete.
        rewindToCurrent_ = false;
        setPc(currentInsn_);
        if (runExecuted_ > 0)
            --runExecuted_;
    }
    insnCount_ += runExecuted_;
    if (err != UC_ERR_OK && lastStop_ != StopReason::Breakpoint && lastStop_ != StopReason::InterruptHook) {
        lastStop_ = StopReason::Fault;
        if (faultKind_ == FaultKind::None) {
            switch (err) {
            case UC_ERR_INSN_INVALID:
                faultKind_ = FaultKind::InvalidInsn;
                break;
            case UC_ERR_READ_UNALIGNED:
            case UC_ERR_WRITE_UNALIGNED:
            case UC_ERR_FETCH_UNALIGNED:
                faultKind_ = FaultKind::Unaligned;
                break;
            default:
                faultKind_ = FaultKind::Other;
                break;
            }
            faultAddress_ = pc();
        }
        if (lastError_.empty())
            lastError_ = uc_strerror(err);
        if (onFault_)
            onFault_(lastError_, pc());
    }
    return lastStop_;
}

void CpuEngine::raiseBusFault(uint32_t addr, bool write, const std::string &reason) {
    lastStop_ = StopReason::Fault;
    faultKind_ = write ? FaultKind::WriteUnmapped : FaultKind::ReadUnmapped;
    faultAddress_ = addr;
    lastError_ = reason;
    rewindToCurrent_ = true;
    if (onFault_)
        onFault_(lastError_, faultAddress_);
    uc_emu_stop(uc_);
}

void CpuEngine::stop() {
    if (uc_)
        uc_emu_stop(uc_);
}
