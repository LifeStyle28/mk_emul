.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

@ Thread code does not use the FPU, the handler does: basic frame, EXC_RETURN 0xFFFFFFF9,
@ and the thread gets back FPCA = 0.
.org 0x100
main:   ldr r1, =0xE000ED88
        ldr r0, =0x00F00000
        str r0, [r1]          @ CPACR: CP10/CP11 full access
        ldr r1, =0xE000E100
        movs r0, #1
        str r0, [r1]          @ enable IRQ0
        ldr r1, =0xE000E200
        str r0, [r1]          @ pend IRQ0
        mrs r3, control
        mov r4, sp
        b .
.ltorg

.org 0x140
irq0:   mov r7, lr
        vmov.f32 s0, #2.0
        vmov r5, s0
        bx lr
.ltorg
