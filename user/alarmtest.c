/* alarmtest — isolate alarm() delivery from custom-handler delivery.
 *
 * With SIG_DFL for SIGALRM, the default action must terminate this
 * process (128 + 14 = 142).  If "pause returned" is printed instead, the
 * alarm never reached the task.
 *
 * Build: make prog NAME=alarmtest   (then: exec /bin/alarmtest)
 */

#include "lib.h"

int main(int argc, char *argv[])
{
    printf("alarmtest: alarm(1) then pause()\n");
    alarm(1);
    pause();
    printf("alarmtest: pause returned - no SIGALRM was delivered\n");
    return 5;
}
