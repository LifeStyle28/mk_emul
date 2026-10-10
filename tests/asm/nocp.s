.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

@ CPACR is 0 after reset: the first FP instruction raises a NOCP UsageFault. r0 = 1 enables
@ UsageFault in SHCSR first (set by the test), otherwise it escalates to HardFault.
.org 0x100
main:   cbz r0, 1f
        ldr r1, =0xE000ED24
        ldr r0, =0x00040000
        str r0, [r1]          @ SHCSR.USGFAULTENA
1:      adr r4, fault
        b fault
.align 2
fault:  vmov.f32 s0, #1.0     @ NOCP: the stacked PC points here
        movs r5, #1           @ never reached
        b .
.ltorg

.org 0x180
handler: ldr r6, [sp, #24]    @ stacked PC
        mrs r7, ipsr
        b .
.ltorg
