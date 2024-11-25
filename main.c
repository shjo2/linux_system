#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <mqueue.h>
#include <assert.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <toy_message.h>

#define NUM_MESSAGES 10

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;

static void
sigchldHandler(int sig)
{
    int status, savedErrno;
    pid_t childPid;

    savedErrno = errno;

    printf("handler: Caught SIGCHLD : %d\n", sig);

    while ((childPid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("handler: Reaped child %ld - ", (long) childPid);
        (NULL, status);
    }

    if (childPid == -1 && errno != ECHILD)
        printf("waitpid");

    printf("handler: returning\n");

    errno = savedErrno;
}

int create_message_queue(mqd_t *msgq_ptr, const char *queue_name, int num_messages, int message_size)
{
    struct mq_attr mq_attrib;
    int mq_errno;
    mqd_t msgq;

    printf("%s name=%s nummsgs=%d\n", __func__, queue_name, num_messages);

    memset(&mq_attrib, 0, sizeof(mq_attrib));
    mq_attrib.mq_msgsize = message_size;
    mq_attrib.mq_maxmsg = num_messages;

    mq_unlink(queue_name);
    msgq = mq_open(queue_name, O_RDWR | O_CREAT | O_CLOEXEC, 0777, &mq_attrib);
    if (msgq == -1) {
        printf("%s queue=%s already exists so try to open\n",
                            __func__, queue_name);
        msgq = mq_open(queue_name, O_RDWR);
        assert(msgq != (mqd_t) -1);
        printf("%s queue=%s opened successfully\n",
                            __func__, queue_name);
        return -1;
    }

    *msgq_ptr = msgq;
    return 0;
}

int main()
{
    pid_t spid, gpid, ipid, wpid;
    int status, savedErrno;
    int sigCnt;
    sigset_t blockMask, emptyMask;
    struct sigaction sa;
    int retcode;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigchldHandler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        printf("sigaction");
        return 0;
    }

    mq_unlink("/watchdog_queue");
    mq_unlink("/monitor_queue");
    mq_unlink("/disk_queue");
    mq_unlink("/camera_queue");

    struct mq_attr attr;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(toy_msg_t);

    printf("attr.mq_maxmsg: %ld | attr.mq_msgsize: %ld\n", 
            attr.mq_maxmsg, attr.mq_msgsize);

    watchdog_queue = mq_open("/watchdog_queue", O_CREAT|O_RDWR, 0666, &attr);
    if(watchdog_queue == (mqd_t)-1){
        perror("mq_open failure");
        printf("ERRno = %d\n", errno);
        exit(0);
    }

    monitor_queue = mq_open("/monitor_queue", O_CREAT|O_RDWR, 0666, &attr);
    if(monitor_queue == (mqd_t)-1){
        perror("mq_open failure");
        printf("ERRno = %d\n", errno);
        exit(0);
    }

    disk_queue = mq_open("/disk_queue", O_CREAT|O_RDWR, 0666, &attr);
    if(disk_queue == (mqd_t)-1){
        perror("mq_open failure");
        printf("ERRno = %d\n", errno);
        exit(0);
    }

    camera_queue = mq_open("/camera_queue", O_CREAT|O_RDWR, 0666, &attr);
    if(camera_queue == (mqd_t)-1){
        perror("mq_open failure");
        printf("ERRno = %d\n", errno);
        exit(0);
    }

    struct mq_attr tmpattr;
    if(mq_getattr(watchdog_queue, &tmpattr) == -1){
        printf("mq_getattr error\n");
    }

    printf("Maximum num of msg on queue: %ld\n", tmpattr.mq_maxmsg);
    printf("Maximum msg size: %ld\n", tmpattr.mq_msgsize);        

    printf("메인 함수입니다.\n");
    printf("시스템 서버를 생성합니다.\n");
    spid = create_system_server();
    printf("웹 서버를 생성합니다.\n");
    wpid = create_web_server();
    printf("입력 프로세스를 생성합니다.\n");
    ipid = create_input();
    printf("GUI를 생성합니다.\n");
    gpid = create_gui();

    waitpid(spid, &status, 0);
    waitpid(gpid, &status, 0);
    waitpid(ipid, &status, 0);
    waitpid(wpid, &status, 0);

    return 0;
}
