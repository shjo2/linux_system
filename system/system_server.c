#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <mqueue.h>
#include <sys/inotify.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/sysmacros.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>

#include <system_server.h>
#include <gui.h>
#include <input.h>
#include <web_server.h>
#include <camera_HAL.h>
#include <toy_message.h>
#include <shared_memory.h>

#define CAMERA_TAKE_PICTURE 1
#define SENSOR_DATA 1
#define BUF_LEN 1024
#define TOY_TEST_FS "./fs"

void signal_exit(void);

pthread_mutex_t system_loop_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  system_loop_cond  = PTHREAD_COND_INITIALIZER;
bool            system_loop_exit = false; 

static mqd_t watchdog_queue;
static mqd_t monitor_queue;
static mqd_t disk_queue;
static mqd_t camera_queue;

static shm_sensor_t *the_sensor_info = NULL;
void set_periodic_timer(long sec_delay, long usec_delay);
void *toy_shm_attach(int shmid);
int toy_shm_detach(void *ptr);

static int timer = 0;
pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t global_timer_sem;
static bool global_timer_stopped;

static void timer_expire_signal_handler()
{
    sem_post(&global_timer_sem);
}

static void system_timeout_handler()
{
    pthread_mutex_lock(&timer_mutex);
    timer++;
    printf("timer: %d\n", timer);
    pthread_mutex_unlock(&timer_mutex);
}

static void *timer_thread(void *not_used)
{
    signal(SIGALRM, timer_expire_signal_handler);
    set_periodic_timer(1, 1);

    while(!global_timer_stopped){
        int rc = sem_wait(&global_timer_sem);
        if(rc == -1 && errno == EINTR)
            continue;
        
        if(rc == -1){
            perror("sem_wait");
            exit(-1);
        }

        system_timeout_handler();
    }
}

void set_periodic_timer(long sec_delay, long usec_delay)
{
	struct itimerval itimer_val = {
		 .it_interval = { .tv_sec = sec_delay, .tv_usec = usec_delay },
		 .it_value = { .tv_sec = sec_delay, .tv_usec = usec_delay }
    };

	setitimer(ITIMER_REAL, &itimer_val, (struct itimerval*)0);
}

int posix_sleep_ms(unsigned int timeout_ms)
{
    struct timespec sleep_time;

    sleep_time.tv_sec = timeout_ms / MILLISEC_PER_SECOND;
    sleep_time.tv_nsec = (timeout_ms % MILLISEC_PER_SECOND) * (NANOSEC_PER_USEC * USEC_PER_MILLISEC);

    return nanosleep(&sleep_time, NULL);
}

void *watchdog_thread(void* arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;

    printf("%s", s);

    while (1) {
        mqretcode = mq_receive(watchdog_queue, (char*)&msg, sizeof(msg), 0);
        if(mqretcode >= 0){
            printf("watchdog_thread: message received\n");
            printf("msg_type: %d\n", msg.msg_type);
            printf("param1: %d\n", msg.param1);
            printf("param2: %d\n", msg.param2);
        }
    }

    return 0;
}

void *monitor_thread(void* arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;

    int shmid;
    struct shmesg *shmp;

    printf("%s", s);

    while (1) {
        mqretcode = (int)mq_receive(monitor_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("monitor_thread: 메시지가 도착했습니다.\n");
        printf("msg.type: %d\n", msg.msg_type);
        printf("msg.param1: %d\n", msg.param1);
        printf("msg.param2: %d\n", msg.param2);
        if (msg.msg_type == SENSOR_DATA) {
            shmid = msg.param1;
            the_sensor_info = toy_shm_attach(shmid);
            printf("sensor temp: %d\n", the_sensor_info->temp);
            printf("sensor info: %d\n", the_sensor_info->press);
            printf("sensor humidity: %d\n", the_sensor_info->humidity);
            toy_shm_detach(the_sensor_info);
        }
    }

    return 0;
}

int get_dir_capacity(char *dirname){
    struct dirent* dent;
    struct stat st;
    DIR *dir;
    int total_size = 0;
    int fsize = 0;
    char path[1024];

    if((dir=opendir(dirname)) == NULL){
        perror("opendir error");
        exit(1);
    }

    while((dent = readdir(dir)) != NULL){
        if((strcmp(dent->d_name, ".") == 0 || strcmp(dent->d_name, "..") == 0))
            continue;
        
        sprintf(path, "%s/%s", dirname, dent->d_name);
        if(lstat(path, &st) != 0)
            continue;
        fsize = st.st_size;

        if(S_ISDIR(st.st_mode)){
            int dirsize = get_dir_capacity(path) + fsize;
            total_size += dirsize;
        }else{
            total_size += fsize;
        }
    }

    return total_size;
}

void *disk_service_thread(void* arg)
{
    char *s = arg;
    int inotifyFd, wd, j;
    char buf[BUF_LEN] __attribute__ ((aligned(8)));
    ssize_t numRead;
    char *p;
    struct inotify_event *event;
    char *directory = TOY_TEST_FS;
    int total_size;

    printf("%s", s);

    if ((inotifyFd=inotify_init()) == -1) {
        perror("inotify_init error");
        exit(1);
    }

    if (inotify_add_watch(inotifyFd, TOY_TEST_FS, IN_CREATE) == -1) {
        perror("inotify_add_watch error");
        exit(1);
    }

    while (1) {
        numRead = read(inotifyFd, buf, BUF_LEN);
        printf("num_read: %ld bytes\n", (long)numRead);
        if(numRead == 0){
            printf("read() from inotify fd returned 0!\n");
            return 0;
        }

        if(numRead == -1) {
            perror("read error");
            exit(1);
        }

        printf("Read %ld bytes from inotify fd\n", (long)numRead);

        for(p = buf; p < buf + numRead;) {
            event = (struct inotify_event *)p;
            p += sizeof(struct inotify_event) + event->len;
        }

        total_size = get_dir_capacity(TOY_TEST_FS);
        printf("Directory size: %d\n", total_size);
    }

    return 0;
}

void *camera_service_thread(void* arg)
{
    char *s = arg;
    int mqretcode;
    toy_msg_t msg;

    printf("%s", s);

   toy_camera_open();

    while (1) {
        mqretcode = (int)mq_receive(camera_queue, (void *)&msg, sizeof(toy_msg_t), 0);
        assert(mqretcode >= 0);
        printf("camera_service_thread: 메시지가 도착했습니다.\n");
        printf("msg.type: %d\n", msg.msg_type);
        printf("msg.param1: %d\n", msg.param1);
        printf("msg.param2: %d\n", msg.param2);
        if (msg.msg_type == CAMERA_TAKE_PICTURE) {
            toy_camera_take_picture();
        }
    }

    return 0;
}

void signal_exit(void)
{
    pthread_mutex_lock(&system_loop_mutex);
    system_loop_exit = true;
    pthread_cond_broadcast(&system_loop_cond);
    pthread_mutex_unlock(&system_loop_mutex);
}

int system_server()
{
    struct itimerspec ts;
    struct sigaction  sa;
    struct sigevent   sev;
    timer_t *tidlist;
    int retcode;
    pthread_t watchdog_thread_tid, monitor_thread_tid, disk_service_thread_tid, camera_service_thread_tid, timer_thread_tid;

    printf("나 system_server 프로세스!\n");

    watchdog_queue = mq_open("/watchdog_queue", O_RDWR);
    assert(watchdog_queue != -1);
    monitor_queue = mq_open("/monitor_queue", O_RDWR);
    assert(monitor_queue != -1);
    disk_queue = mq_open("/disk_queue", O_RDWR);
    assert(disk_queue != -1);
    camera_queue = mq_open("/camera_queue", O_RDWR);
    assert(camera_queue != -1);

    retcode = pthread_create(&watchdog_thread_tid, NULL, watchdog_thread, "watchdog thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&monitor_thread_tid, NULL, monitor_thread, "monitor thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&disk_service_thread_tid, NULL, disk_service_thread, "disk service thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&camera_service_thread_tid, NULL, camera_service_thread, "camera service thread\n");
    assert(retcode == 0);
    retcode = pthread_create(&timer_thread_tid, NULL, timer_thread, "timer thread\n");
    assert(retcode == 0);

    printf("system init done.  waiting...");


    pthread_mutex_lock(&system_loop_mutex);
    while (system_loop_exit == false) {
        pthread_cond_wait(&system_loop_cond, &system_loop_mutex);
    }
    pthread_mutex_unlock(&system_loop_mutex);

    printf("<== system\n");

    while (system_loop_exit == false) {
        sleep(1);
    }

    return 0;
}

int create_system_server()
{
    pid_t systemPid;
    const char *name = "system_server";

    printf("여기서 시스템 프로세스를 생성합니다.\n");

    switch (systemPid = fork()) {
    case -1:
        printf("fork failed\n");
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
