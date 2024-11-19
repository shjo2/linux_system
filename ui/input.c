#include <stdio.h>
#include <sys/prctl.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <execinfo.h>


int input()
{
    printf("나 input 프로세스!\n");

    /* 여기서 SIGSEGV 시그널 등록 */

    while (1) {
        sleep(1);
    }

    return 0;
}

int create_input()
{
    pid_t systemPid;
    const char *name = "input";

    printf("여기서 input 프로세스를 생성합니다.\n");

    switch (systemPid = fork()) {
    case -1:
        printf("fork failed\n");
    case 0:
        if (prctl(PR_SET_NAME, (unsigned long) name) < 0)
            perror("prctl()");
        input();
        break;
    default:
        break;
    }

    return 0;
}
