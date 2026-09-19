#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

struct uc_struct;
typedef struct uc_struct uc_engine;

class MmioBus;

class CpuEngine {
public:
    CpuEngine();
    ~CpuEngine();

    CpuEngine(const CpuEngine &) = delete;
    CpuEngine &operator=(const CpuEngine &) = delete;

    bool open(std::string *error);
    void close();
    bool isOpen() const { return uc_ != nullptr; }

    void mapRam(uint32_t addr, uint32_t size);
    void mapMmio(uint32_t addr, uint32_t size, MmioBus *bus);
    void writeMem(uint32_t addr, const void *data, size_t size);
    void readMem(uint32_t addr, void *data, size_t size) const;
    uint32_t read32(uint32_t addr) const;
    void write32(uint32_t addr, uint32_t value);

    uint32_t reg(int unicornReg) const;
    void setReg(int unicornReg, uint32_t value);

    uint32_t pc() const;
    uint32_t sp() const;
    uint32_t lr() const;
    uint32_t xpsr() const;
    uint32_t msp() const;
    uint32_t psp() const;
    uint32_t control() const;
    uint32_t ipsr() const;
    uint32_t primask() const;
    void setPc(uint32_t value);
    void setSp(uint32_t value);
    void setLr(uint32_t value);
    void setXpsr(uint32_t value);
    void setMsp(uint32_t value);
    void setPsp(uint32_t value);
    void setControl(uint32_t value);
    void setPrimask(uint32_t value);

    void snapshotRegs(uint32_t out[16]) const;
    void readBanked();

    enum class StopReason { None, Quantum, Breakpoint, Fault, InterruptHook, Halted };

    StopReason run(uint64_t maxInsns);
    void stop();

    void addBreakpoint(uint32_t addr);
    void removeBreakpoint(uint32_t addr);
    void clearBreakpoints();
    bool hasBreakpoint(uint32_t addr) const;
    std::vector<uint32_t> breakpoints() const;

    using InterruptHandler = std::function<void(uint32_t intno)>;
    void setInterruptHandler(InterruptHandler handler);

    using FaultHandler = std::function<void(const std::string &msg, uint32_t addr)>;
    void setFaultHandler(FaultHandler handler);

    uint64_t instructionCount() const { return insnCount_; }
    const std::string &lastError() const { return lastError_; }
    StopReason lastStop() const { return lastStop_; }
    uint32_t faultAddress() const { return faultAddress_; }

    uc_engine *raw() { return uc_; }

private:
    void installHooks();
    void rebuildBreakpointHooks();

    uc_engine *uc_ = nullptr;
    InterruptHandler onInterrupt_;
    FaultHandler onFault_;
    std::unordered_set<uint32_t> breakpoints_;
    std::vector<uint64_t> bpHooks_;
    uint64_t codeHook_ = 0;
    uint64_t intrHook_ = 0;
    uint64_t memHook_ = 0;
    uint64_t insnCount_ = 0;
    uint32_t faultAddress_ = 0;
    StopReason lastStop_ = StopReason::None;
    std::string lastError_;
    bool skipNextBreakpoint_ = false;
};
