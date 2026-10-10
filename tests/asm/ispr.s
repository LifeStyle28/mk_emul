.syntax unified
.cpu cortex-m4
.thumb

.org 0x100
main:   ldr r1, =0xE000E100  @ NVIC_ISER0
        movs r0, #1
        str r0, [r1]         @ enable IRQ0
        ldr r1, =0xE000E200  @ NVIC_ISPR0
        str r0, [r1]         @ pend IRQ0: handler must run before the next instruction
        movs r5, #1
        b .
.org 0x140
irq0:   mov r6, r5
        adds r7, #1
        bx lr
.ltorg
