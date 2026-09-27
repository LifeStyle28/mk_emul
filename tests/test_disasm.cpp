#include "core/Disassembler.h"

#include <gtest/gtest.h>

namespace {

TEST(Disassembler, DecodesThumbInstructions) {
    Disassembler d;
    std::string err;
    ASSERT_TRUE(d.open(&err)) << err;
    // adds r0,#1 ; b .-2 ; bx lr
    const uint8_t code[] = {0x01, 0x30, 0xFD, 0xE7, 0x70, 0x47};
    const auto lines = d.disassemble(code, sizeof(code), 0x08000040u, 0x08000042u, {0x08000044u});
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0].mnemonic, "adds");
    EXPECT_EQ(lines[0].bytes, "01 30");
    EXPECT_EQ(lines[1].mnemonic, "b");
    EXPECT_TRUE(lines[1].current);
    EXPECT_EQ(lines[2].mnemonic, "bx");
    EXPECT_EQ(lines[2].op, "lr");
    EXPECT_TRUE(lines[2].breakpoint);
}

TEST(Disassembler, DecodesThumb2WideInstruction) {
    Disassembler d;
    ASSERT_TRUE(d.open(nullptr));
    const uint8_t code[] = {0x4F, 0xF0, 0x01, 0x00}; // mov.w r0, #1
    const auto lines = d.disassemble(code, sizeof(code), 0x08000000u, 0, {});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].size, 4u);
    EXPECT_EQ(lines[0].mnemonic, "mov.w");
}

} // namespace
