.syntax unified
.cpu cortex-m4
.fpu fpv4-sp-d16
.thumb

@ CPACR = privileged access only: FP works in privileged thread mode, and raises NOCP after
@ the thread drops to unprivileged (CONTROL.nPRIV = 1).
.org 0x100
main:   ldr r1, =0xE000ED88
        ldr r0, =0x00500000
        str r0, [r1]          @ CP10/CP11 = 01: privileged only
        vmov.f32 s0, #1.0     @ privileged: allowed
        vmov r5, s0
        movs r0, #1
        msr control, r0       @ nPRIV
        isb
        adr r4, fault
        b fault
.align 2
fault:  vmov.f32 s1, #2.0     @ unprivileged: NOCP
        b .
.ltorg

.org 0x180
hardfault: ldr r6, [sp, #24]  @ stacked PC
        b .
.ltorg
