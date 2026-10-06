/*
    stub.ld - PS5Instrumentalizer
    Links the in-game hook stub into one flat, address-0 image:
    header, code, read-only data, data, bss. The image is later copied
    verbatim (objcopy -O binary) into memory allocated inside the game.
*/

SECTIONS
{
    . = 0;
    .header : ALIGN(16) { KEEP(*(.header)) }
    .text   : ALIGN(16) { *(.text .text.*) }
    .rodata : ALIGN(16) { *(.rodata .rodata.*) }
    .data   : ALIGN(16) { *(.data .data.*) }
    .bss    : ALIGN(16) { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(16);
    __stub_end = .;

    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.llvm_addrsig) *(.got*) *(.plt*) *(.dynamic) *(.interp) }
}
