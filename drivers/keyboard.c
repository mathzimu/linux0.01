#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/tty.h>

static unsigned char keymap[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0,
};

static unsigned char shift_map[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0,
};

static int shift_pressed = 0;

void kbd_interrupt_handler(int scancode)
{
    unsigned char c;
    int is_break;

    is_break = scancode & 0x80;
    scancode &= 0x7F;

    if (scancode == 42 || scancode == 54) {
        shift_pressed = !is_break;
        return;
    }

    if (scancode >= 128) return;

    if (!is_break) {
        if (shift_pressed)
            c = shift_map[scancode];
        else
            c = keymap[scancode];

        if (c) {
            if (tty_table[0].read_cnt < TTY_BUF_SIZE) {
                tty_table[0].read_buf[tty_table[0].read_head] = c;
                tty_table[0].read_head = (tty_table[0].read_head + 1) % TTY_BUF_SIZE;
                tty_table[0].read_cnt++;
            }
            if (tty_table[0].read_waiter) {
                tty_table[0].read_waiter->state = TASK_RUNNING;
                tty_table[0].read_waiter = NULL;
            }
        }
    }
}
