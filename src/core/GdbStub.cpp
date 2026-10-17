#include "core/GdbStub.h"

#include "core/Addresses.h"
#include "core/Machine.h"

#include <unicorn/arm.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

const char kHex[] = "0123456789abcdef";

std::string hex32(uint32_t v) {
    // Target byte order (little-endian), as in 'g' / 'p' replies.
    std::string s;
    for (int i = 0; i < 4; ++i) {
        const uint8_t b = static_cast<uint8_t>(v >> (8 * i));
        s += kHex[b >> 4];
        s += kHex[b & 15];
    }
    return s;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool decodeHex(const std::string &hex, std::vector<uint8_t> *out) {
    if (hex.size() % 2)
        return false;
    out->clear();
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hexDigit(hex[i]);
        const int lo = hexDigit(hex[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out->push_back(static_cast<uint8_t>(hi * 16 + lo));
    }
    return true;
}

uint32_t leWord(const std::vector<uint8_t> &b, size_t at) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i)
        v = (v << 8) | (at + i < b.size() ? b[at + i] : 0u);
    return v;
}

uint32_t parseHex(const std::string &s, size_t *pos) {
    uint32_t v = 0;
    while (*pos < s.size() && hexDigit(s[*pos]) >= 0)
        v = v * 16 + static_cast<uint32_t>(hexDigit(s[(*pos)++]));
    return v;
}

// Address ranges the emulator maps (memory and peripherals); anything else is an error for gdb,
// so a backtrace through garbage stops with "Cannot access memory" instead of reading zeros.
bool mapped(uint32_t addr, uint32_t len) {
    struct R {
        uint32_t base, size;
    };
    static const R ranges[] = {
        {mcu::kFlashAlias, mcu::kFlashSize}, {mcu::kFlashBase, mcu::kFlashSize}, {mcu::kCcmBase, mcu::kCcmSize},
        {mcu::kSramBase, mcu::kSramSize},    {mcu::kApbAhbBase, mcu::kApbAhbSize}, {mcu::kAhb2Base, mcu::kAhb2Size},
        {mcu::kPpbBase, mcu::kPpbSize},
    };
    const uint64_t end = static_cast<uint64_t>(addr) + len;
    for (const R &r : ranges) {
        if (addr >= r.base && end <= static_cast<uint64_t>(r.base) + r.size)
            return true;
    }
    return false;
}

} // namespace

GdbStub::GdbStub(Machine &machine, std::function<void(const std::string &)> send, Target target)
    : m_(machine)
    , send_(std::move(send))
    , target_(std::move(target)) {}

void GdbStub::connected() {
    in_.clear();
    noAck_ = false;
    running_ = false;
}

std::string GdbStub::targetXml() {
    std::string x = "<?xml version=\"1.0\"?>\n<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
                    "<target version=\"1.0\">\n<architecture>arm</architecture>\n"
                    "<feature name=\"org.gnu.gdb.arm.m-profile\">\n";
    char line[160];
    for (int i = 0; i < 13; ++i) {
        std::snprintf(line, sizeof(line), "<reg name=\"r%d\" bitsize=\"32\" regnum=\"%d\" type=\"uint32\" group=\"general\"/>\n", i, i);
        x += line;
    }
    x += "<reg name=\"sp\" bitsize=\"32\" regnum=\"13\" type=\"data_ptr\" group=\"general\"/>\n"
         "<reg name=\"lr\" bitsize=\"32\" regnum=\"14\" type=\"uint32\" group=\"general\"/>\n"
         "<reg name=\"pc\" bitsize=\"32\" regnum=\"15\" type=\"code_ptr\" group=\"general\"/>\n"
         "<reg name=\"xpsr\" bitsize=\"32\" regnum=\"16\" type=\"uint32\" group=\"general\"/>\n"
         "</feature>\n<feature name=\"org.gnu.gdb.arm.vfp\">\n";
    for (int i = 0; i < 16; ++i) {
        std::snprintf(line, sizeof(line), "<reg name=\"d%d\" bitsize=\"64\" regnum=\"%d\" type=\"ieee_double\" group=\"float\"/>\n", i, 17 + i);
        x += line;
    }
    x += "<reg name=\"fpscr\" bitsize=\"32\" regnum=\"33\" type=\"int\" group=\"float\"/>\n"
         "</feature>\n<feature name=\"org.gnu.gdb.arm.m-system\">\n"
         "<reg name=\"msp\" bitsize=\"32\" regnum=\"34\" type=\"data_ptr\" group=\"system\"/>\n"
         "<reg name=\"psp\" bitsize=\"32\" regnum=\"35\" type=\"data_ptr\" group=\"system\"/>\n"
         "</feature>\n<feature name=\"org.mk_emul.cortex-m.special\">\n"
         "<reg name=\"primask\" bitsize=\"32\" regnum=\"36\" type=\"uint32\" group=\"system\"/>\n"
         "<reg name=\"basepri\" bitsize=\"32\" regnum=\"37\" type=\"uint32\" group=\"system\"/>\n"
         "<reg name=\"faultmask\" bitsize=\"32\" regnum=\"38\" type=\"uint32\" group=\"system\"/>\n"
         "<reg name=\"control\" bitsize=\"32\" regnum=\"39\" type=\"uint32\" group=\"system\"/>\n"
         "</feature>\n</target>\n";
    return x;
}

void GdbStub::sendPacket(const std::string &payload) {
    unsigned sum = 0;
    for (unsigned char c : payload)
        sum += c;
    char tail[4];
    std::snprintf(tail, sizeof(tail), "#%02x", sum & 0xFFu);
    send_("$" + payload + tail);
}

void GdbStub::feed(const char *data, size_t len) {
    in_.append(data, len);
    for (;;) {
        // Skip acks and noise; Ctrl-C (0x03) interrupts a running target.
        size_t i = 0;
        while (i < in_.size() && in_[i] != '$') {
            if (in_[i] == '\x03' && running_ && target_.interrupt)
                target_.interrupt();
            ++i;
        }
        in_.erase(0, i);
        if (in_.empty())
            return;
        const size_t hash = in_.find('#');
        if (hash == std::string::npos || hash + 2 >= in_.size())
            return; // incomplete packet
        std::string body = in_.substr(1, hash - 1);
        in_.erase(0, hash + 3);
        if (!noAck_)
            send_("+");
        // Binary escapes ('}' + (c ^ 0x20)) only appear in X/vFile packets we do not support;
        // decode anyway so a stray escape cannot corrupt a reply.
        std::string p;
        p.reserve(body.size());
        for (size_t k = 0; k < body.size(); ++k) {
            if (body[k] == '}' && k + 1 < body.size())
                p += static_cast<char>(body[++k] ^ 0x20);
            else
                p += body[k];
        }
        handlePacket(p);
    }
}

void GdbStub::stopReply(int signal) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "T%02xthread:1;", signal & 0xFF);
    sendPacket(buf);
}

void GdbStub::stopped(int signal) {
    if (!running_)
        return;
    running_ = false;
    stopReply(signal);
}

std::string GdbStub::readRegister(int n) const {
    const CpuEngine &c = m_.cpu();
    if (n >= 0 && n <= 12)
        return hex32(c.reg(UC_ARM_REG_R0 + n));
    switch (n) {
    case 13:
        return hex32(c.sp());
    case 14:
        return hex32(c.lr());
    case 15:
        return hex32(c.pc() & ~1u);
    case 16:
        return hex32(c.xpsr());
    case 33:
        return hex32(c.fpscr());
    case 34:
        return hex32(c.msp());
    case 35:
        return hex32(c.psp());
    case 36:
        return hex32(c.primask());
    case 37:
        return hex32(c.basepri());
    case 38:
        return hex32(c.faultmask());
    case 39:
        return hex32(c.control());
    default:
        break;
    }
    if (n >= 17 && n <= 32) {
        const int d = n - 17; // d<n> = s<2n> (low word) : s<2n+1>
        return hex32(c.sreg(2 * d)) + hex32(c.sreg(2 * d + 1));
    }
    return {};
}

void GdbStub::writeRegister(int n, const std::string &hex) {
    std::vector<uint8_t> b;
    if (!decodeHex(hex, &b))
        return;
    CpuEngine &c = m_.cpu();
    const uint32_t v = leWord(b, 0);
    if (n >= 0 && n <= 12)
        c.setReg(UC_ARM_REG_R0 + n, v);
    else if (n == 13)
        c.setSp(v);
    else if (n == 14)
        c.setLr(v);
    else if (n == 15)
        c.setPc(v | 1u); // stay in Thumb state (Unicorn takes T from bit 0)
    else if (n == 16)
        c.setXpsr(v | 0x01000000u);
    else if (n >= 17 && n <= 32) {
        c.setSreg(2 * (n - 17), v);
        c.setSreg(2 * (n - 17) + 1, leWord(b, 4));
    } else if (n == 33)
        c.setFpscr(v);
    else if (n == 34)
        c.setMsp(v);
    else if (n == 35)
        c.setPsp(v);
    else if (n == 36)
        c.setPrimask(v);
    else if (n == 37)
        c.setReg(UC_ARM_REG_BASEPRI, v);
    else if (n == 38)
        c.setReg(UC_ARM_REG_FAULTMASK, v);
    else if (n == 39)
        c.setControl(v);
}

std::string GdbStub::readMemory(uint32_t addr, uint32_t len) const {
    if (len > 4096)
        len = 4096;
    if (!mapped(addr, len))
        return "E01";
    std::vector<uint8_t> buf(len);
    m_.readMemory(addr, buf.data(), len);
    std::string s;
    s.reserve(len * 2);
    for (uint8_t b : buf) {
        s += kHex[b >> 4];
        s += kHex[b & 15];
    }
    return s;
}

bool GdbStub::writeMemory(uint32_t addr, uint32_t len, const std::string &hex) {
    std::vector<uint8_t> b;
    if (!decodeHex(hex, &b) || b.size() != len || !mapped(addr, len))
        return false;
    m_.writeMemory(addr, b.data(), b.size());
    return true;
}

std::string GdbStub::monitor(const std::string &cmd) {
    if (cmd == "reset" || cmd == "reset halt" || cmd == "reset init") {
        if (target_.reset)
            target_.reset();
        else
            m_.reset();
        return "OK";
    }
    // Unknown command: tell the user via console output ('O' packet), then OK.
    std::string text = "mk_emul: monitor commands: reset\n";
    std::string o = "O";
    for (unsigned char c : text) {
        o += kHex[c >> 4];
        o += kHex[c & 15];
    }
    sendPacket(o);
    return "OK";
}

void GdbStub::startResume(uint32_t start, uint32_t end) {
    running_ = true;
    if (target_.resume)
        target_.resume(start, end);
}

void GdbStub::stepOnce() {
    m_.step();
    if (target_.stateChanged)
        target_.stateChanged();
    stopReply(kSigTrap);
}

void GdbStub::handlePacket(const std::string &p) {
    if (p.empty()) {
        sendPacket("");
        return;
    }
    if (running_) {
        // Only Ctrl-C is valid while the target runs (all-stop mode).
        return;
    }
    const char cmd = p[0];
    size_t pos = 1;
    switch (cmd) {
    case '?':
        sendPacket("S05");
        return;
    case 'g': {
        std::string s;
        for (int i = 0; i < kRegCount; ++i)
            s += readRegister(i);
        sendPacket(s);
        return;
    }
    case 'G': {
        size_t at = 1;
        for (int i = 0; i < kRegCount && at < p.size(); ++i) {
            const size_t width = (i >= 17 && i <= 32) ? 16 : 8;
            writeRegister(i, p.substr(at, width));
            at += width;
        }
        if (target_.stateChanged)
            target_.stateChanged();
        sendPacket("OK");
        return;
    }
    case 'p': {
        const int n = static_cast<int>(parseHex(p, &pos));
        const std::string v = readRegister(n);
        sendPacket(v.empty() ? "E01" : v);
        return;
    }
    case 'P': {
        const int n = static_cast<int>(parseHex(p, &pos));
        if (pos >= p.size() || p[pos] != '=' || n >= kRegCount) {
            sendPacket("E01");
            return;
        }
        writeRegister(n, p.substr(pos + 1));
        if (target_.stateChanged)
            target_.stateChanged();
        sendPacket("OK");
        return;
    }
    case 'm': {
        const uint32_t addr = parseHex(p, &pos);
        ++pos; // ','
        const uint32_t len = parseHex(p, &pos);
        sendPacket(readMemory(addr, len));
        return;
    }
    case 'M': {
        const uint32_t addr = parseHex(p, &pos);
        ++pos;
        const uint32_t len = parseHex(p, &pos);
        const bool ok = pos < p.size() && p[pos] == ':' && writeMemory(addr, len, p.substr(pos + 1));
        if (ok && target_.stateChanged)
            target_.stateChanged();
        sendPacket(ok ? "OK" : "E01");
        return;
    }
    case 'Z':
    case 'z': {
        // Z0 software / Z1 hardware breakpoint: both are the emulator's breakpoints.
        // Watchpoints (Z2-Z4) are not supported: the empty reply tells gdb so.
        if (p.size() < 2 || (p[1] != '0' && p[1] != '1')) {
            sendPacket("");
            return;
        }
        pos = 3;
        const uint32_t addr = parseHex(p, &pos);
        if (cmd == 'Z')
            m_.addBreakpoint(addr);
        else
            m_.removeBreakpoint(addr);
        sendPacket("OK");
        return;
    }
    case 'c':
    case 'C':
        startResume(0, 0);
        return;
    case 's':
    case 'S':
        stepOnce();
        return;
    case 'H':
    case 'T':
        sendPacket("OK");
        return;
    case 'D':
        sendPacket("OK");
        return;
    case 'k':
        return;
    default:
        break;
    }

    auto startsWith = [&](const char *prefix) { return p.compare(0, std::strlen(prefix), prefix) == 0; };
    if (startsWith("qSupported")) {
        sendPacket("PacketSize=4000;qXfer:features:read+;QStartNoAckMode+;vContSupported+");
    } else if (p == "QStartNoAckMode") {
        sendPacket("OK");
        noAck_ = true;
    } else if (startsWith("qXfer:features:read:target.xml:")) {
        size_t at = std::strlen("qXfer:features:read:target.xml:");
        const uint32_t off = parseHex(p, &at);
        ++at;
        const uint32_t len = parseHex(p, &at);
        const std::string xml = targetXml();
        if (off >= xml.size()) {
            sendPacket("l");
        } else {
            const std::string chunk = xml.substr(off, len);
            sendPacket((off + chunk.size() >= xml.size() ? "l" : "m") + chunk);
        }
    } else if (startsWith("qXfer:")) {
        sendPacket("E00");
    } else if (p == "vCont?") {
        sendPacket("vCont;c;C;s;S;r");
    } else if (startsWith("vCont;")) {
        // All-stop with one thread: the first action decides.
        const char action = p.size() > 6 ? p[6] : 'c';
        if (action == 'r') {
            size_t at = 7;
            const uint32_t start = parseHex(p, &at);
            ++at;
            const uint32_t end = parseHex(p, &at);
            startResume(start, end);
        } else if (action == 's' || action == 'S') {
            stepOnce();
        } else {
            startResume(0, 0);
        }
    } else if (p == "qC") {
        sendPacket("QC1");
    } else if (p == "qfThreadInfo") {
        sendPacket("m1");
    } else if (p == "qsThreadInfo") {
        sendPacket("l");
    } else if (p == "qAttached") {
        sendPacket("1");
    } else if (startsWith("qSymbol")) {
        sendPacket("OK");
    } else if (startsWith("qRcmd,")) {
        std::vector<uint8_t> b;
        std::string cmdText;
        if (decodeHex(p.substr(6), &b))
            cmdText.assign(b.begin(), b.end());
        sendPacket(monitor(cmdText));
    } else {
        sendPacket(""); // not supported
    }
}
