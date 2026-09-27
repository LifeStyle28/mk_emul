.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

@ Thread code uses the FPU, then an interrupt clobbers S0/S15/FPSCR: the extended frame
@ must bring them back.
.org 0x100
main:   ldr r1, =0xE000ED88
        ldr r0, =0x00F00000
        str r0, [r1]          @ CPACR: CP10/CP11 full access
        vmov.f32 s0, #1.0     @ first FP instruction: CONTROL.FPCA = 1
        vmov.f32 s15, #3.0
        ldr r0, =0x03C00000   @ FPSCR: DN, FZ, RMode = towards zero
        vmsr fpscr, r0
        ldr r1, =0xE000E100
        movs r0, #1
        str r0, [r1]          @ enable IRQ0
        ldr r1, =0xE000E200
        str r0, [r1]          @ pend IRQ0: taken before the next instruction
        vmov r5, s0           @ 1.0 again after the handler
        vmov r6, s15          @ 3.0
        vmrs r4, fpscr        @ 0x03C00000
        mrs r3, control       @ FPCA restored
        b .
.ltorg

.org 0x180
irq0:   mov r7, lr            @ EXC_RETURN
        mrs r2, control       @ FPCA is 0 on entry
        mov r8, sp            @ frame size
        vmov.f32 s0, #2.0     @ clobber caller-saved FP registers and FPSCR
        vmov.f32 s15, #-1.0
        movs r0, #0
        vmsr fpscr, r0
        mrs r9, control       @ FPCA = 1: the handler has its own FP context now
        bx lr
.ltorg
