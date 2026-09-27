.syntax unified
.cpu cortex-m4
.thumb

.org 0x100
main:   ldr r1, =0xE000ED04  @ ICSR
        ldr r0, =0x10000000  @ PENDSVSET
        str r0, [r1]
        movs r5, #1
        b .
.org 0x140
pendsvh: mov r6, r5
        bx lr
.ltorg
