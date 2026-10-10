.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

@ FPDSCR holds the FPSCR of a new FP context: the handler's FP code runs with it, and the
@ interrupted thread gets its own FPSCR back from the extended frame.
.org 0x100
main:   ldr r1, =0xE000ED88
        ldr r0, =0x00F00000
        str r0, [r1]          @ CPACR: FPU full access
        dsb
        isb
        ldr r1, =0xE000EF3C
        ldr r0, =0x03C00000
        str r0, [r1]          @ FPDSCR = DN | FZ | RMode = round towards zero
        vmov.f32 s0, #1.0     @ thread FP context, FPSCR = 0
        vmrs r4, fpscr
        svc #0
        vmrs r6, fpscr        @ restored from the frame
        b .
.ltorg

.org 0x180
svc_handler:
        vmrs r5, fpscr        @ new FP context of the handler
        vmov.f32 s1, #2.0
        bx lr
.ltorg
