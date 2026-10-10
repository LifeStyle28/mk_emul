.syntax unified
.cpu cortex-m4
.thumb

.org 0x100
main:   ldr r1, =0xE000E400
        movs r0, #0x50
        strb r0, [r1]        @ IRQ0 priority 0x50
        movs r0, #0x40
        strb r0, [r1, #1]    @ IRQ1 priority 0x40 (more urgent)
        ldr r1, =0xE000ED0C  @ AIRCR
        ldr r0, [r4]         @ r4 -> AIRCR value chosen by the test (in RAM)
        str r0, [r1]
        ldr r1, =0xE000E100
        movs r0, #3
        str r0, [r1]         @ enable IRQ0, IRQ1
        ldr r1, =0xE000E200
        movs r0, #1
        str r0, [r1]         @ pend IRQ0
        b .
.org 0x140
irq0:   ldr r1, =0xE000E200
        movs r0, #2
        str r0, [r1]         @ pend IRQ1 from inside IRQ0
        movs r5, #1
        bx lr
.ltorg
.org 0x160
irq1:   mov r6, r5           @ r6 = 0: IRQ1 preempted IRQ0; 1: ran after it
        bx lr
