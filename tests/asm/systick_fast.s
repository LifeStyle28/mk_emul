.syntax unified
.cpu cortex-m4
.thumb

.org 0x100
main:   ldr r1, =0xE000E010
        movs r0, #100
        str r0, [r1, #4]     @ LOAD = 100: period 101 cycles, shorter than a 256-insn chunk
        movs r0, #7
        str r0, [r1]         @ ENABLE | TICKINT | CLKSOURCE
loop:   adds r3, #1
        b loop
.org 0x140
sth:    adds r4, #1
        bx lr
.ltorg
