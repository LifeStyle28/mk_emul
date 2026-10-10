.syntax unified
.cpu cortex-m4
.thumb

.org 0x100
main:   ldr r2, =0x20000000
        ldr r3, [r2]
        adds r3, #1
        str r3, [r2]         @ boot counter in SRAM (survives a system reset)
        cmp r3, #3
        bge done
        ldr r1, =0xE000ED0C
        ldr r0, =0x05FA0004  @ VECTKEY | SYSRESETREQ
        str r0, [r1]
        b .
done:   b done
.ltorg
