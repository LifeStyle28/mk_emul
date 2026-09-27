.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

@ IRQ0 uses the FPU and is preempted by IRQ1, which clobbers S1: "return to handler with
@ FP context" (EXC_RETURN 0xFFFFFFE1) must restore it for IRQ0.
.org 0x100
main:   ldr r1, =0xE000ED88
        ldr r0, =0x00F00000
        str r0, [r1]          @ CPACR: CP10/CP11 full access
        ldr r1, =0xE000E400
        movs r0, #0x40
        strb r0, [r1]         @ IRQ0 priority 0x40
        movs r0, #0x20
        strb r0, [r1, #1]     @ IRQ1 priority 0x20 (more urgent)
        ldr r1, =0xE000E100
        movs r0, #3
        str r0, [r1]          @ enable IRQ0, IRQ1
        ldr r1, =0xE000E200
        movs r0, #1
        str r0, [r1]          @ pend IRQ0
        mov r4, sp
        b .
.ltorg

.org 0x140
irq0:   vmov.f32 s1, #5.0
        ldr r1, =0xE000E200
        movs r0, #2
        str r0, [r1]          @ pend IRQ1: preempts right here
        vmov r5, s1           @ 5.0 again
        bx lr
.ltorg

.org 0x180
irq1:   mov r7, lr            @ EXC_RETURN 0xFFFFFFE1
        vmov.f32 s1, #-2.0
        bx lr
.ltorg
