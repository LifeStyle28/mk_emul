ENTRY(Reset_Handler)
MEMORY
{
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
  CCM (xrw)  : ORIGIN = 0x10000000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : {
    *(.text*)
    *(.rodata*)
    KEEP(*(.init))
    KEEP(*(.fini))
    _etext = .;
  } > FLASH
  .data : {
    _sdata = .;
    *(.data*)
    _edata = .;
  } > RAM AT > FLASH
  _sidata = LOADADDR(.data);
  .bss : {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    _ebss = .;
  } > RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.note.GNU-stack) }
  PROVIDE(_init = 0);
  PROVIDE(_fini = 0);
  PROVIDE(__exidx_start = 0);
  PROVIDE(__exidx_end = 0);
  /* Heap start for newlib's _sbrk (malloc, and printf/snprintf in newlib-nano): the heap grows
     from the end of .bss towards the stack. Without it any firmware using them fails to link
     with "undefined reference to `end'". */
  . = ALIGN(8);
  PROVIDE(end = .);
  PROVIDE(_end = .);
}
