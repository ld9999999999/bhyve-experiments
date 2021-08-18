OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH("i386")


SECTIONS {
        . = 0x0000;

        /DISCARD/ : { *(.dynstr*) }
        /DISCARD/ : { *(.dynamic*) }
        /DISCARD/ : { *(.interp*) }
        /DISCARD/ : { *(.gnu*) }

        .start : AT(0x0000) { KEEP(*(.start)); }

        .text  : { *(.text*); }
        .rodata : {
                *(.rodata);
                *(.rodata*);
                _rodata_end = .;
                *(.got);
                *(.got.plt);
                ASSERT(. == _rodata_end, "no GOT/PLT allowed");
        }

        /DISCARD/ : { *(.plt*) }

        . = 0x1fff0;
        .suite_end : AT(0x1fff0) { KEEP(*(.suite_end)); }

        /DISCARD/ : {
       *(.bss*) *(.comment)
       *(COMMON) *(.discard*) *(.eh_frame) *(.note*)
    }
}

ENTRY(_suite_end)

