OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(startup_32)

SECTIONS
{
    /* Kernel loaded at 0x10800 (after 4-sector setup at 0x10000) */
    . = 0x10800;

    .text : {
        _text = .;
        *(.text)
        *(.text.*)
        _etext = .;
    }

    .data : {
        _data = .;
        *(.data)
        *(.data.*)
        _edata = .;
    }

    .bss : {
        _bss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        _ebss = .;
    }

    _end = .;
}
