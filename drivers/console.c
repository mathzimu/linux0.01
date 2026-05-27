#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/io.h>

#define VIDEO_BASE 0xB8000
#define VIDEO_SIZE (80*25*2)
#define CRT_INDEX 0x3D4
#define CRT_DATA 0x3D5
#define CRT_CURSOR_HI 14
#define CRT_CURSOR_LO 15

static unsigned short *video_mem = (unsigned short *)VIDEO_BASE;
static int cursor_x = 0;
static int cursor_y = 0;

static void set_cursor(int x, int y)
{
    unsigned short pos = y * 80 + x;
    outb(CRT_CURSOR_HI, CRT_INDEX);
    outb(pos >> 8, CRT_DATA);
    outb(CRT_CURSOR_LO, CRT_INDEX);
    outb(pos & 0xFF, CRT_DATA);
}

static void scroll(void)
{
    int i;
    for (i = 0; i < (80 * (25 - 1)); i++)
        video_mem[i] = video_mem[i + 80];
    for (i = 80 * (25 - 1); i < 80 * 25; i++)
        video_mem[i] = 0x0700;
    cursor_y = 24;
    cursor_x = 0;
}

void con_init(void)
{
    int i;
    for (i = 0; i < 80 * 25; i++)
        video_mem[i] = 0x0720;
    cursor_x = 0;
    cursor_y = 0;
    set_cursor(0, 0);
}

void con_write(struct tty_struct *tty)
{
    int i;
    char c;

    while (tty->write_cnt > 0) {
        c = tty->write_buf[tty->write_tail];
        tty->write_tail = (tty->write_tail + 1) % TTY_BUF_SIZE;
        tty->write_cnt--;

        switch (c) {
        case '\n':
            cursor_y++;
            cursor_x = 0;
            break;
        case '\r':
            cursor_x = 0;
            break;
        case '\t':
            cursor_x = (cursor_x + 8) & ~7;
            break;
        case '\b':
            if (cursor_x > 0) cursor_x--;
            video_mem[cursor_y * 80 + cursor_x] = 0x0720;
            break;
        default:
            if (c >= 32) {
                video_mem[cursor_y * 80 + cursor_x] = 0x0700 | (unsigned short)c;
                cursor_x++;
            }
            break;
        }

        if (cursor_x >= 80) {
            cursor_x = 0;
            cursor_y++;
        }

        if (cursor_y >= 25)
            scroll();
    }

    set_cursor(cursor_x, cursor_y);
}
