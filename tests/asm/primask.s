.syntax unified
.cpu cortex-m4
.thumb

.org 0x100
main:   cpsid i
        ldr r1, =0xE000E100
        movs r0, #1
        str r0, [r1]
        ldr r1, =0xE000E200
        str r0, [r1]         @ pended while PRIMASK=1
        movs r5, #1
        cpsie i              @ taken right here
        movs r5, #2
        b .
.org 0x140
irq0:   mov r6, r5
        bx lr
.ltorg
