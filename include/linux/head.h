#ifndef _HEAD_H
#define _HEAD_H

#ifndef _DESC_STRUCT_DEFINED
#define _DESC_STRUCT_DEFINED
struct desc_struct {
    unsigned long a, b;
};
#endif
typedef struct desc_struct desc_table[256];

extern desc_table _gdt, _idt;

#define GDT_SIZE (132)
#define IDT_SIZE (256)

/* GDT layout: index 0=null, 1=kernel_code, 2=kernel_data, 3=user_code, 4=user_data */
#define KERNEL_CS 0x08  /* index 1, RPL 0 */
#define KERNEL_DS 0x10  /* index 2, RPL 0 */
#define USER_CS   0x1B  /* index 3, RPL 3 */
#define USER_DS   0x23  /* index 4, RPL 3 */

#define PAGE_DIRECTORY 0x100000
#define PAGE_TABLE_0   0x101000

#endif
