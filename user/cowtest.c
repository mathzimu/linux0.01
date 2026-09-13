/* cowtest — M3 regression: fork() must give the child a private copy of
 * the data segment, the heap and the stack.
 *
 * Before M3 this was false in the most direct way possible: fork() copied
 * the user stack but shared code and heap, so the child's writes were
 * visible in the parent (and a child that execve'd rewrote the parent's
 * code).  Now every user page is shared copy-on-write, and this program
 * checks both halves of that promise:
 *
 *   1. the parent's data/heap/stack are unchanged after the child wrote
 *      to its own copies;
 *   2. the child really did see its own values (i.e. we are not just
 *      observing that the child never ran).
 */
#include "lib.h"

static int global_counter = 100;        /* .data: one COW page */
static int bss_counter;                 /* .bss: another one */

int main(void)
{
    int pid;
    unsigned long status;
    int *heap = (int *)malloc(sizeof(int));
    int stack_marker = 7;

    if (!heap) {
        printf("cowtest: malloc failed\n");
        return 1;
    }
    *heap = 111;

    printf("cowtest: parent before fork: global=%d bss=%d heap=%d stack=%d\n",
           global_counter, bss_counter, *heap, stack_marker);

    pid = fork();
    if (pid == 0) {
        /* Child: write to every kind of page it shares with the parent. */
        global_counter = 222;
        bss_counter = 333;
        *heap = 444;
        stack_marker = 555;

        printf("cowtest: child sees: global=%d bss=%d heap=%d stack=%d\n",
               global_counter, bss_counter, *heap, stack_marker);
        if (global_counter == 222 && bss_counter == 333 &&
            *heap == 444 && stack_marker == 555)
            printf("cowtest: child PASS\n");
        else
            printf("cowtest: child FAIL (writes did not stick)\n");
        exit(0);
    }

    if (pid < 0) {
        printf("cowtest: fork failed\n");
        return 1;
    }

    waitpid(pid, &status, 0);

    printf("cowtest: parent after child exit: global=%d bss=%d heap=%d "
           "stack=%d\n", global_counter, bss_counter, *heap, stack_marker);

    if (global_counter == 100 && bss_counter == 0 &&
        *heap == 111 && stack_marker == 7) {
        printf("cowtest: PASS (fork gave the child private pages)\n");
        return 0;
    }

    printf("cowtest: FAIL (child wrote through to the parent - no COW)\n");
    return 1;
}
