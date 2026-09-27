.syntax unified
.cpu cortex-m4
.thumb

.org 0x100
main:   movs r5, #1
        svc #0
        movs r5, #2
        b .
.org 0x140
svch:   ldr r4, [sp, #24]    @ stacked PC = return address (r4: not restored on return)
        mov r6, r5
        bx lr
