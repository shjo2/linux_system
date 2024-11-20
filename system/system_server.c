#include <stdio.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>

static int timer = 0;

void sighandler_timer(int sig) {
    timer++;
    time_t rawTime;
    struct tm* formattedTime;
    rawTime = time(NULL);

    formattedTime = localtime(&rawTime);

    int year = formattedTime->tm_year + 1900;
    int month = formattedTime->tm_mon + 1;
    int day = formattedTime->tm_mday;
    int hour = formattedTime->tm_hour;
    int min = formattedTime->tm_min;
    int sec = formattedTime->tm_sec;

    printf("Current Time Info : %d year %d month %d day %d:%d:%d\n"
           "    timer_count : %d\n",
           year, month, day, hour, min, sec, timer);
}

int posix_sleep_ms(unsigned int timeout_ms) {
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / 1000; 
    sleep_time.tv_nsec = (timeout_ms % 1000) * 1000000;

    return nanosleep(&sleep_time, NULL);
}

int system_server() {
    struct itimerval ts;
    struct sigaction sa;

    printf("system_server Process...!\n");

    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = 0;
    sa.sa_handler = sighandler_timer;

    ts.it_value.tv_sec = 5;     
    ts.it_value.tv_usec = 0;
    ts.it_interval.tv_sec = 5;  
    ts.it_interval.tv_usec = 0;

    if (sigaction(SIGALRM, &sa, NULL) == -1) { 
        fprintf(stderr, "timer signal init error\n");
        exit(EXIT_FAILURE);
    }

    if (setitimer(ITIMER_REAL, &ts, NULL) == -1) { 
        fprintf(stderr, "set timer error\n");
        exit(EXIT_FAILURE);
    }

    while (1) {
        posix_sleep_ms(5000); 
    }

    return 0;
}

int create_system_server() {
    pid_t systemPid;
    const char *name = "system_server";

    printf("여기서 시스템 프로세스를 생성합니다.\n");

    switch (systemPid = fork()) {
        case -1:
            printf("fork failed\n");
            exit(EXIT_FAILURE);
        case 0:
            if (prctl(PR_SET_NAME, (unsigned long) name) < 0)
                perror("prctl()");
            system_server();
            break;
        default:
            break;
    }

    return 0;
}