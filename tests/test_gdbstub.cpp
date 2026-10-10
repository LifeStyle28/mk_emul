// GDB remote protocol server (what the UI debugger's arm-none-eabi-gdb talks to).
#include "TestUtil.h"

#include "core/GdbStub.h"

#include <gtest/gtest.h>
#include <unicorn/arm.h>

#include <cstdio>
#include <string>

namespace {

constexpr uint32_t F = FlashImage::kBase;
constexpr uint32_t kStackTop = 0x20001000u;

// 0x40: adds r0,#1 ; b 0x40
FlashImage loopImage() {
    FlashImage img;
    img.word(F, kStackTop).vector(1, F + 0x40);
    img.thumb(F + 0x40, {0x3001, 0xE7FD});
    return img;
}

std::string frame(const std::string &payload) {
    unsigned sum = 0;
    for (unsigned char c : payload)
        sum += c;
    char tail[4];
    std::snprintf(tail, sizeof(tail), "#%02x", sum & 0xFFu);
    return "$" + payload + tail;
}

struct GdbStubTest : ::testing::Test {
    Machine m;
    std::string out;
    int resumes = 0;
    uint32_t rangeStart = 0, rangeEnd = 0;
    int interrupts = 0;
    int changes = 0;
    GdbStub stub{m, [this](const std::string &s) { out += s; },
                 GdbStub::Target{[this](uint32_t s, uint32_t e) {
                                     ++resumes;
                                     rangeStart = s;
                                     rangeEnd = e;
                                 },
                                 [this] { ++interrupts; }, [this] { ++changes; }, nullptr}};

    void SetUp() override {
        std::string err;
        ASSERT_TRUE(m.initialize(&err)) << err;
        loadImage(m, loopImage());
        stub.connected();
    }
    // Sends one packet, returns the reply payload ("" if none), checks ack and checksum.
    std::string ask(const std::string &payload, bool expectAck = true) {
        out.clear();
        const std::string f = frame(payload);
        stub.feed(f.data(), f.size());
        std::string s = out;
        if (expectAck) {
            EXPECT_EQ(s.substr(0, 1), "+") << payload;
            s.erase(0, 1);
        }
        if (s.empty())
            return {};
        EXPECT_EQ(s[0], '$') << s;
        const size_t hash = s.rfind('#');
        const std::string body = s.substr(1, hash - 1);
        EXPECT_EQ(s.substr(hash), frame(body).substr(frame(body).size() - 3)) << "checksum";
        return body;
    }
    std::string reg32(uint32_t v) {
        char b[9];
        std::snprintf(b, sizeof(b), "%02x%02x%02x%02x", v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, v >> 24);
        return b;
    }
};

TEST_F(GdbStubTest, HandshakeAndTargetDescription) {
    const std::string sup = ask("qSupported:multiprocess+;swbreak+;hwbreak+;xmlRegisters=arm");
    EXPECT_NE(sup.find("qXfer:features:read+"), std::string::npos) << sup;
    EXPECT_NE(sup.find("PacketSize="), std::string::npos) << sup;
    EXPECT_EQ(ask("?"), "S05");
    EXPECT_EQ(ask("vCont?"), "vCont;c;C;s;S;r");

    // The description arrives in chunks: 'm' = more follows, 'l' = last.
    std::string xml;
    for (uint32_t off = 0;;) {
        char req[64];
        std::snprintf(req, sizeof(req), "qXfer:features:read:target.xml:%x,%x", off, 0x100);
        const std::string r = ask(req);
        ASSERT_FALSE(r.empty());
        xml += r.substr(1);
        off += static_cast<uint32_t>(r.size() - 1);
        if (r[0] == 'l')
            break;
        ASSERT_EQ(r[0], 'm');
    }
    EXPECT_EQ(xml, GdbStub::targetXml());
    EXPECT_NE(xml.find("org.gnu.gdb.arm.m-profile"), std::string::npos);
    EXPECT_NE(xml.find("org.gnu.gdb.arm.vfp"), std::string::npos);
}

TEST_F(GdbStubTest, ReadsAndWritesRegisters) {
    const std::string g = ask("g");
    ASSERT_EQ(g.size(), 17u * 8 + 16u * 16 + 7u * 8) << "r0-pc, xpsr, d0-d15, fpscr, msp..control";
    EXPECT_EQ(g.substr(13 * 8, 8), reg32(kStackTop)) << "sp";
    EXPECT_EQ(g.substr(15 * 8, 8), reg32(F + 0x40)) << "pc";
    EXPECT_EQ(g.substr(16 * 8, 8), reg32(0x01000000u)) << "xpsr: Thumb";
    EXPECT_EQ(ask("pf"), reg32(F + 0x40));

    EXPECT_EQ(ask("P0=" + reg32(0x12345678u)), "OK");
    EXPECT_EQ(m.cpu().reg(UC_ARM_REG_R0), 0x12345678u);
    EXPECT_EQ(ask("Pf=" + reg32(F + 0x42)), "OK");
    EXPECT_EQ(m.cpu().pc(), F + 0x42);
    EXPECT_GE(changes, 2) << "the UI refreshes after debugger writes";

    // d0 = s1:s0
    m.cpu().setSreg(0, 0x3F800000u); // 1.0f
    m.cpu().setSreg(1, 0x40000000u); // 2.0f
    EXPECT_EQ(ask("p11"), reg32(0x3F800000u) + reg32(0x40000000u));
}

TEST_F(GdbStubTest, ReadsAndWritesMemory) {
    EXPECT_EQ(ask("M20000100,4:efbeadde"), "OK");
    uint8_t b[4];
    m.readMemory(0x20000100u, b, 4);
    EXPECT_EQ(b[0], 0xEF);
    EXPECT_EQ(b[3], 0xDE);
    EXPECT_EQ(ask("m20000100,4"), "efbeadde");
    EXPECT_EQ(ask("m8000040,4"), "0130fde7") << "the loop code in flash";
    EXPECT_EQ(ask("m60000000,4"), "E01") << "unmapped (FSMC)";
}

TEST_F(GdbStubTest, BreakpointContinueAndStopReply) {
    EXPECT_EQ(ask("Z0,8000042,2"), "OK");
    EXPECT_TRUE(m.cpu().hasBreakpoint(F + 0x42));
    EXPECT_EQ(ask("vCont;c"), "") << "no reply until the target stops";
    EXPECT_EQ(resumes, 1);
    EXPECT_TRUE(stub.running());
    EXPECT_EQ(rangeEnd, 0u);

    // The owner runs the machine; it stops on the breakpoint.
    EXPECT_EQ(runFor(m, 100), CpuEngine::StopReason::Breakpoint);
    out.clear();
    stub.stopped(GdbStub::kSigTrap);
    EXPECT_EQ(out, frame("T05thread:1;"));
    EXPECT_FALSE(stub.running());

    EXPECT_EQ(ask("z0,8000042,2"), "OK");
    EXPECT_FALSE(m.cpu().hasBreakpoint(F + 0x42));
    EXPECT_EQ(ask("Z2,20000000,4"), "") << "watchpoints are not supported";
}

TEST_F(GdbStubTest, StepAndRangeStep) {
    EXPECT_EQ(ask("s"), "T05thread:1;");
    EXPECT_EQ(m.cpu().instructionCount(), 1u);
    EXPECT_EQ(m.cpu().pc(), F + 0x42);

    EXPECT_EQ(ask("vCont;r8000040,8000042:1"), "");
    EXPECT_EQ(rangeStart, F + 0x40);
    EXPECT_EQ(rangeEnd, F + 0x42);
}

TEST_F(GdbStubTest, InterruptWhileRunning) {
    ask("c");
    ASSERT_TRUE(stub.running());
    out.clear();
    stub.feed("\x03", 1);
    EXPECT_EQ(interrupts, 1);
    stub.stopped(GdbStub::kSigInt);
    EXPECT_EQ(out, frame("T02thread:1;"));
}

TEST_F(GdbStubTest, NoAckModeAndMonitorReset) {
    EXPECT_EQ(ask("QStartNoAckMode"), "OK");
    EXPECT_EQ(ask("qAttached", false), "1");
    m.step();
    EXPECT_EQ(ask("qRcmd,7265736574", false), "OK") << "monitor reset";
    EXPECT_EQ(m.cpu().pc(), F + 0x40);
}

} // namespace
