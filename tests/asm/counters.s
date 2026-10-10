.syntax unified
.cpu cortex-m4
.thumb

.org 0x100
main:   ldr r1, =0xE000E010
        ldr r0, =0x00FFFFFF
        str r0, [r1, #4]     @ SysTick LOAD
        movs r0, #5
        str r0, [r1]         @ ENABLE | CLKSOURCE=HCLK, no interrupt
        ldr r2, [r1, #8]     @ VAL
        .rept 10
        nop
        .endr
        ldr r3, [r1, #8]     @ VAL, 11 cycles later
        ldr r1, =0x40023840
        movs r0, #1
        str r0, [r1]         @ RCC_APB1ENR.TIM2EN
        ldr r1, =0x40000000
        str r0, [r1]         @ TIM2 CEN (PSC 0, ARR 0xFFFFFFFF)
        ldr r4, [r1, #0x24]  @ CNT
        .rept 10
        nop
        .endr
        ldr r5, [r1, #0x24]  @ CNT, 11 cycles later
        b .
.ltorg
