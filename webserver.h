#ifndef _WEBSERVER_H
#define _WEBSERVER_H
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404
#define LOGSPLIT 99999
#define LOGTIMEDIFF 9999
#define PLAINLOG 9998
#define TIMEBUFFERSIZE 1000 // size of buffer related to time
#define PROCESS_NUM 11
#define NAME_LEN 50
#define DESCRIPTION_LEN 100
#define COST_TIME_NUM 7
#define SERVER_CODE "THREAD-GDSF"
#define LOG_BUFFER_LEN BUFSIZE*10
#define MAX_EVENTS 10
#define MAX_FILE_LINE_NUM 103831

#ifndef COUNT_TIME
#define COUNT_TIME
#endif

typedef struct {
    int hit;
    int fd;
} WebParam;

typedef struct LogFile {
    const char* log_name;
    int log_len;
    char out_buffer[LOG_BUFFER_LEN];
    int idx;
} LogFile;

typedef struct TimeCounter {
    double cost_time[COST_TIME_NUM];
    double total_cost_time;
    int invokes;
    pthread_mutex_t mutex;
} TimeCounter;


typedef struct CostDetail {
    double cost_time;
    const char name[NAME_LEN];
    const char description[DESCRIPTION_LEN];
} CostDetail;

typedef struct FileEventInfo{
    int fd;               /* file being sent */
    char buf[BUFSIZE];    /* current chunk of file */
    int buf_len;          /* bytes in buffer */
    int buf_used;         /* bytes used so far; <= m_buf_len */
    char* store_buf;

    enum { IDLE, SENDING, READING, WRITING } m_state; /* what we're doing */
}FileEventInfo;

pthread_cond_t global_cost_counter_cond;
pthread_mutex_t global_cost_counter_mutex;

void LogFileInit(struct LogFile* logf, const char* log_name);
unsigned long GetFileSize(const char* path);
double TimeDiff(struct timeval* pre_mannual);
void TimeToBuffer(char* buffer);
void Logger(int type, const char* s1, const char* s2, int socket_fd, int is_out,
            struct LogFile* logf);
void* Web(void* data);
void* CounterThread(void* data);
void* LoadFileToHash(void* arg);
#endif
