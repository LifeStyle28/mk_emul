.syntax unified
.cpu cortex-m4
.thumb

@ Thread code on PSP (as RTOS tasks run), first privileged, then unprivileged (CONTROL = 3).
@ Exceptions must stack onto PSP, run the handler on MSP and return to PSP / nPRIV intact.
.org 0x100
main:   ldr r0, =0x2001F000
        msr psp, r0
        movs r0, #2
        msr control, r0       @ SPSEL: thread uses PSP
        isb
        ldr r1, =0xE000E100
        movs r0, #1
        str r0, [r1]          @ enable IRQ0
        ldr r1, =0xE000E200
        str r0, [r1]          @ pend IRQ0: taken here
        mov r4, sp            @ 0x2001F000: back on PSP
        mrs r3, control       @ 2
        movs r0, #3
        msr control, r0       @ + nPRIV: unprivileged from now on
        isb
        svc #0
        mov r8, sp            @ 0x2001F000
        mrs r9, control       @ 3: still unprivileged, on PSP
        b .
.ltorg

.org 0x180
irq0:   mov r7, lr            @ EXC_RETURN 0xFFFFFFFD
        mov r6, sp            @ handler runs on MSP
        mrs r5, psp           @ PSP after stacking: 0x2001F000 - 0x20
        bx lr
.ltorg

.org 0x1C0
svc:    mov r11, lr           @ 0xFFFFFFFD
        mov r10, sp           @ MSP
        bx lr
.ltorg
