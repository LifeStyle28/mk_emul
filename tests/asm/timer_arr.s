.syntax unified
.cpu cortex-m4
.thumb

.org 0x100
main:   ldr r1, =0x40023840
        movs r0, #1
        str r0, [r1]          @ RCC_APB1ENR.TIM2EN
        ldr r1, =0xE000E100
        ldr r0, =0x10000000
        str r0, [r1]          @ enable IRQ28 (TIM2)
        ldr r1, =0x40000000
        movs r0, #1
        str r0, [r1, #0x0C]   @ DIER.UIE
        movs r0, #20
        str r0, [r1, #0x2C]   @ ARR = 20
        movs r0, #1
        str r0, [r1]          @ CEN: update in 21 cycles, inside the current chunk
        movs r5, #0
loop:   adds r5, #1
        b loop
.ltorg
.org 0x140
tim2:   ldr r1, =0x40000000
        movs r0, #0
        str r0, [r1]          @ stop the timer
        str r0, [r1, #0x10]   @ clear UIF
        mov r6, r5            @ loop iterations before the interrupt
        adds r7, #1
        bx lr
.ltorg
