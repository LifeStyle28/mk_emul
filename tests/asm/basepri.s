.syntax unified
.cpu cortex-m4
.thumb

.org 0x100
main:   ldr r1, =0xE000E400  @ NVIC_IPR0 byte 0 = IRQ0 priority
        movs r0, #0x40
        strb r0, [r1]
        msr basepri, r0      @ mask priority >= 0x40
        ldr r1, =0xE000E100
        movs r0, #1
        str r0, [r1]
        ldr r1, =0xE000E200
        str r0, [r1]
        movs r5, #1
        movs r0, #0
        msr basepri, r0      @ unmask: taken right here
        movs r5, #2
        b .
.org 0x140
irq0:   mov r6, r5
        bx lr
.ltorg
