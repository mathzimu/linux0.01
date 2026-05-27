#ifndef _ASM_MEMORY_H
#define _ASM_MEMORY_H

#define copy_page(from, to) \
    __asm__ volatile("cld; rep; movsl" \
                     : : "S"(from), "D"(to), "c"(1024) \
                     : "memory")

#endif
