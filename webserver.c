#include "webserver.h"
#include "hashtable.h"
#include "memswitch.h"
#include "thread_pool.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef SIGCLD
#define SIGCLD SIGCHLD
#endif

const int WORKING_THREAD_NUM = THREAD_NUM - 1;

char* url_prefix = "http://127.0.0.1:9191/";
char* list_location = "/home/oliver/os-prac/web/lab7/lru/web/src/list";
int url_prefix_length = 0;

int hash_prime = 103837;
int file_size = 0;

struct epoll_event log_in_file_ev, read_socket_ev, write_socket_ev,
    read_file_ev, write_file_ev;

char* file_buffer = NULL;

HashTable* table;
CacheList* cachelist;

struct TimeCounter global_cost_counter;

struct {
    char* ext;
    char* filetype;
} extensions[] = { { "gif", "image/gif" }, { "jpg", "image/jpg" },
    { "jpeg", "image/jpeg" }, { "png", "image/png" }, { "ico", "image/ico" },
    { "zip", "image/zip" }, { "gz", "image/gz" }, { "tar", "image/tar" },
    { "htm", "text/htm" }, { "html", "text/html" }, { 0, 0 } };

int setNonblocking(int fd)
{
    int flags;

    /* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    /* Otherwise, use the old way of doing it */
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}

unsigned long GetFileSize(const char* path)
{
    unsigned long filesize = -1;
    struct stat statbuff;
    if (stat(path, &statbuff) < 0) {
        return filesize;
    } else {
        filesize = statbuff.st_size;
    }
    return filesize;
}

double TimeDiff(struct timeval* pre_mannual)
{
    static struct timeval pre;
    struct timeval now;
    gettimeofday(&now, NULL);
    double diff;
    diff = now.tv_sec + now.tv_usec * 1e-6;
    if (pre_mannual != NULL) {
        diff -= pre_mannual->tv_sec + pre_mannual->tv_usec * 1e-6;
        *pre_mannual = now;
    } else
        diff -= pre.tv_sec + pre.tv_usec * 1e-6;
    pre = now;
    return diff;
}

void TimeToBuffer(char* buffer)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm now;
    localtime_r((time_t*)(&(tv.tv_sec)), &now);
    sprintf(buffer, "Time: %d-%d-%d-%d-%d-%d-%6ld\t", now.tm_year + 1900,
        now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec,
        tv.tv_usec);
}

void LogFileInit(struct LogFile* logf, const char* log_name)
{
    logf->log_len = 0;
    logf->log_name = log_name;
    logf->out_buffer[0] = 0;
}

void Logger(int type, const char* s1, const char* s2, int socket_fd, int is_out,
    struct LogFile* logf)
{
    int fd;
    char logbuffer[BUFSIZE * 10];
    char buffer[BUFSIZE * 10];
    char timebuffer[TIMEBUFFERSIZE];
    char* splitter = "------------------------------------------------";
    if (type != LOGSPLIT)
        TimeToBuffer(timebuffer);
    else
        timebuffer[0] = 0;

    switch (type) {
    case ERROR:
        (void)sprintf(logbuffer,
            "ERROR: %s:%s Errno=%d exiting"
            "pid=%d",
            s1, s2, errno, getpid());
        break;
    case FORBIDDEN:
        (void)write(socket_fd,
            "HTTP/1.1 403 Forbidden\n"
            "Content-Length: 185\nConnection: close\n:"
            "Content-Type: text/html\n\n<html><head>\n"
            "<title>403 Forbidden</title>\n</head><body>\n"
            "<h1>Forbidden</h1>\nThe requested URL, file type"
            " or operation is not allowed on this simple"
            " static file webserver.\n</body></html>\n",
            271);
        (void)sprintf(logbuffer, "FORBIDDEN: %s:%s", s1, s2);
        break;
    case NOTFOUND:
        (void)write(socket_fd,
            "HTTP/1.1 404 Not Found\n"
            "Content-Length: 136\nConnection: close\n:"
            "Content-Type: text/html\n\n<html><head>\n"
            "<title>404 Not Found</title>\n</head><body>\n"
            "<h1>Not Found</h1>\nThe requested URL was not"
            " found on this server.\n"
            "</body></html>\n",
            224);
        (void)sprintf(logbuffer, "NOT FOUND: %s:%s", s1, s2);
        break;
    case LOG:
        (void)sprintf(logbuffer, "INFO: %s:%s:%d", s1, s2, socket_fd);
        break;
    case LOGSPLIT:
        (void)sprintf(logbuffer, "%s %d %s%s", s1, socket_fd, s2, splitter);
        break;
    case LOGTIMEDIFF:
        (void)sprintf(logbuffer, "Time cost analysis:\n%s", s1);
        break;
    case PLAINLOG:
        break;
    }

    if (type != PLAINLOG) {
        sprintf(buffer, "%s%s%c", timebuffer, logbuffer, '\n');
        if (is_out == 0)
            (logf->log_len)
                += sprintf(logf->out_buffer + (logf->log_len), "%s", buffer);

        if (logf->log_name != NULL && is_out == 1
            && (fd = open(logf->log_name, O_CREAT | O_WRONLY | O_APPEND, 0644))
                >= 0) {
            (void)write(fd, buffer, strlen(buffer));
            (void)close(fd);
        }
    } else {
        if (logf->log_name != NULL && is_out == 1
            && (fd = open(logf->log_name, O_CREAT | O_WRONLY | O_APPEND, 0644))
                >= 0) {
            (void)write(fd, logf->out_buffer, logf->log_len);
            (void)close(fd);
            logf->log_len = 0;
            logf->out_buffer[0] = 0;
        }
    }
}
void* CounterThread(void* data)
{
    const struct ThreadPool* pool;
    struct ThreadInfo* threadinfo;
    struct Thread** threads;
    threadinfo = (*(ThreadInfo**)(data));
    CostDetail* details = (*(CostDetail**)(data + sizeof(ThreadInfo*)));
    pool = (*(ThreadPool**)(data + sizeof(ThreadInfo*) + sizeof(CostDetail*)));
    struct LogFile logf;
    LogFileInit(&logf, SERVER_CODE "-time_cost.log");

    if (!(pool->is_alive))
        return NULL;

    threads = pool->threads;
    int num = THREAD_NUM;
    double cost_time[num][COST_TIME_NUM + 1];
    char logbuffer[LOG_BUFFER_LEN];
    int invokes[num];
    int idx;
    int i;
    int length;
    int total_invokes = 0;
    extern long long hit;
    extern long long loss;
    extern long long collide;
    extern long long collide_length;
#ifdef DEBUG
    int _hit = 1;
#endif
    double total_time = 0;
    memset(cost_time, 0, sizeof(cost_time));
    memset(invokes, 0, sizeof(invokes));

    Logger(LOG, "counter_thread working", "", getpid(), 1, &logf);
    while (1 && pool->is_alive) {
        /* auto shutdown in debug mod after a certain times of visit */
#ifdef DEBUG
        if (_hit >= DEBUG_MAX_TIMES)
            break;
#endif
        sleep(30); // count every 30 seconds
        total_time = 0;
        total_invokes = 0;
        memset(invokes, 0, sizeof(invokes));
        memset(cost_time, 0, sizeof(cost_time));
        length = 0;
        for (idx = 0; idx < num; ++idx) {
            if (idx == *(threadinfo->id))
                continue;
            pthread_mutex_lock(&(threads[idx]->threadinfo->counter->mutex));
            for (i = 0; i < COST_TIME_NUM; ++i) {
                cost_time[idx][i]
                    = threads[idx]->threadinfo->counter->cost_time[i];
                threads[idx]->threadinfo->counter->cost_time[i] = 0;
            }
            cost_time[idx][COST_TIME_NUM]
                = threads[idx]->threadinfo->counter->total_cost_time;
            threads[idx]->threadinfo->counter->total_cost_time = 0;
            invokes[idx] = threads[idx]->threadinfo->counter->invokes;
            threads[idx]->threadinfo->counter->invokes = 0;
            pthread_mutex_unlock(&(threads[idx]->threadinfo->counter->mutex));
        }
        length = 0;
        for (idx = 0; idx < num; ++idx) {
            total_invokes += invokes[idx];
            total_time += cost_time[idx][COST_TIME_NUM];
            for (int i = 0; i < COST_TIME_NUM; ++i)
                details[i].cost_time += cost_time[idx][i];
            /*length used for appending the buffer*/
            if (idx != *threadinfo->id) {
                length += sprintf(logbuffer + length,
                    "thread "
                    "%d:\tinvokes:%d\ttotal_cost_time:%.6lf\tavg_cost_time:%."
                    "6lf",
                    idx, invokes[idx], cost_time[idx][COST_TIME_NUM],
                    cost_time[idx][COST_TIME_NUM]
                        / (invokes[idx] == 0 ? 1 : invokes[idx]));

                for (int j = 0; j < COST_TIME_NUM; ++j) {
                    length += sprintf(logbuffer + length, "\t%s:%.6lf",
                        details[j].name,
                        cost_time[idx][j]
                            / (invokes[idx] == 0 ? 1 : invokes[idx]));
                }
                length += sprintf(logbuffer + length, "\n");
            } else {
                length += sprintf(logbuffer + length,
                    "thread %d:\tthis is the counter thread\n", idx);
            }
        }
        for (int i = 0; i < COST_TIME_NUM; ++i) {
            length += sprintf(logbuffer + length,
                "%s "
                "\t\t%s:\n\t\t\t\ttotal_cost:%.6lf\n\t\t\t\tavg_invokes_cost_"
                "time:%"
                ".6lf\n",
                details[i].name, details[i].description, details[i].cost_time,
                details[i].cost_time
                    / (total_invokes == 0 ? 1 : total_invokes));
            details[i].cost_time = 0;
        }
        length += sprintf(logbuffer + length,
            "total_invokes:%d\n"
            "avg_invokes:%d\ntotal_cost_time:%.6lf\navg_thread_cost_time:%."
            "6lf\navg_"
            "invokes_cost_time:%.6lf\n",
            total_invokes, total_invokes / ((num - 1) == 0 ? 1 : (num - 1)),
            total_time, total_time / ((num - 1) == 0 ? 1 : (num - 1)),
            total_time / (total_invokes == 0 ? 1 : total_invokes));
        length += sprintf(logbuffer + length,
            "cache hit ratio: %.2lf%%"
            "\n"
            "hit: %lld\nloss: %lld\n"
            "collide ratio: %.2lf%%\n"
            "collide: %lld\n"
            "collide_length: %lld\n",
            ((double)(hit) / ((hit + loss) == 0 ? 1 : (hit + loss))) * 100, hit,
            loss,
            ((((double)collide) / (total_invokes == 0 ? 1 : total_invokes))
                * 100),
            collide, collide_length);
        hit = 0;
        loss = 0;
        collide = 0;
        collide_length = 0;
#ifdef CACHE_INSPECT
        pthread_mutex_lock(&(cachelist->mutex));
        long long int tmp_total = 0;
        CacheItem* next = cachelist->front;
        while (next) {
            tmp_total += next->cont->length;
            next = next->next;
        }
        length += sprintf(logbuffer + length,
            "current mem_taken:%lld, max_mem:%d\tcachelist_num:%lld\n",
            tmp_total, MEM_MAX, cachelist->num);
        pthread_mutex_unlock(&(cachelist->mutex));
#endif
        sprintf(logbuffer + length,
            "Notes: those abbreviatons correspond to the time stamps logged "
            "in function web\n\n");
        Logger(LOGSPLIT, "\n\nLog", SERVER_CODE " start", hit, 0, &logf);
        Logger(LOGTIMEDIFF, logbuffer, "", hit, 0, &logf);
        Logger(LOGSPLIT, "Log", "end", hit, 0, &logf);
        Logger(PLAINLOG, "", "", hit, 1, &logf);
        ++hit;
    }
    return NULL;
}

void* Web(void* data)
{
    int fd;
    int hit;
    struct ThreadInfo* threadinfo;

    int j, buflen;
    /* int file_fd; */
    long i, ret, len;
    double timetmp;
    char* fstr;
    char timeDiffLog[TIMEBUFFERSIZE];   // log time diff
    char timeTmpBuffer[TIMEBUFFERSIZE]; // used for sprintf to convert doubles
    /*time base for TimeDiff, cause multi-thread can't use default static base*/
    struct timeval pre;
    struct TimeCounter cost_counter;
    char buffer[BUFSIZE + 1];
    memset(buffer, 0, sizeof(0));
    memset(&(cost_counter.cost_time), 0, sizeof(cost_counter.cost_time));
    cost_counter.invokes = 0;
    cost_counter.total_cost_time = 0;
    timeDiffLog[0] = 0;
    fd = ((WebParam*)(data + sizeof(ThreadInfo*)))->fd;
    hit = ((WebParam*)(data + sizeof(ThreadInfo*)))->hit;
    threadinfo = *(ThreadInfo**)(data);
    threadinfo->logf->idx = 0;

    TimeDiff(&pre); // time counter init

    ret = read(fd, buffer, BUFSIZE);
    timetmp = TimeDiff(&pre);
    sprintf(
        timeTmpBuffer, "Reading socket content cost %lf seconds.\n", timetmp);
    strcat(timeDiffLog, timeTmpBuffer);
    cost_counter.cost_time[threadinfo->logf->idx++] = timetmp;
    cost_counter.total_cost_time += timetmp;

    Logger(LOGSPLIT, "\n\nLog",
        SERVER_CODE " "
                    "start",
        hit, 0, (threadinfo->logf));
    if (ret == 0 || ret == -1) {
        Logger(FORBIDDEN, "failed to read browser request", "", fd, 0,
            (threadinfo->logf));
    } else {
        if (ret > 0 && ret < BUFSIZE)
            buffer[ret] = 0;
        else
            buffer[0] = 0;
        for (i = 0; i < ret; i++)
            if (buffer[i] == '\r' || buffer[i] == '\n')
                buffer[i] = '*';
        Logger(LOG, "request", buffer, hit, 0, (threadinfo->logf));
        if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
            Logger(FORBIDDEN, "Only simple GET operation supported", buffer, fd,
                0, (threadinfo->logf));
        }
        for (i = 4; i < BUFSIZE; i++) {
            if (buffer[i] == ' ') {
                buffer[i] = 0;
                break;
            }
        }
        for (j = 0; j < i - 1; j++)
            if (buffer[j] == '.' && buffer[j + 1] == '.') {
                Logger(FORBIDDEN,
                    "Parent directory (..)"
                    " path names not supported",
                    buffer, fd, 0, (threadinfo->logf));
            }
        if (!strncmp(&buffer[0], "GET /\0", 6)
            || !strncmp(&buffer[0], "get /\0", 6))
            (void)strcpy(buffer, "GET /index.html");
        buflen = strlen(buffer);
        fstr = (char*)0;
        for (i = 0; extensions[i].ext != 0; i++) {
            len = strlen(extensions[i].ext);
            if (!strncmp(&buffer[buflen - len], extensions[i].ext, len)) {
                fstr = extensions[i].filetype;
                break;
            }
        }
        if (fstr == 0)
            Logger(FORBIDDEN, "file extension type not supported", buffer, fd,
                0, (threadinfo->logf));

        char file_name[NAME_LEN];
        sprintf(file_name, "%s", &buffer[5]);

        /* if ((file_fd = open(file_name, O_RDONLY)) == -1) {
         *     Logger(NOTFOUND, "failed to open file", file_name, fd, 0,
         *         (threadinfo->logf));
         * } */
        len = GetFileSize(file_name);
        if (len == -1)
            Logger(NOTFOUND, "failed to open file", &buffer[5], fd, 0,
                (threadinfo->logf));
        Logger(LOG, "SEND", &buffer[5], hit, 0, (threadinfo->logf));

        (void)sprintf(buffer,
            "HTTP/1.1 200 OK\nServer: nweb/%d.0\n"
            "Content-Length: %ld\nConnection:close\n"
            "Content-Type: %s\n\n",
            VERSION, len, fstr);
        Logger(LOG, "Header", buffer, hit, 0, (threadinfo->logf));

        timetmp = TimeDiff(&pre);
        sprintf(timeTmpBuffer,
            "Validation checking for socket content"
            " and loging cost %lf seconds.\n",
            timetmp);
        strcat(timeDiffLog, timeTmpBuffer);
        if(threadinfo->logf->idx++>7)
        {
            Error("logf->idx > 7", "webserver.log");

        }
        cost_counter.cost_time[threadinfo->logf->idx++] = timetmp;
        cost_counter.total_cost_time += timetmp;

        (void)write(fd, buffer, strlen(buffer));

        timetmp = TimeDiff(&pre);
        sprintf(timeTmpBuffer, "Writing header to socket cost %lf seconds.\n",
            timetmp);
        strcat(timeDiffLog, timeTmpBuffer);
        cost_counter.cost_time[threadinfo->logf->idx++] = timetmp;
        cost_counter.total_cost_time += timetmp;

        /* int cont_buff_p = 0;
         * int len_onetime = 0; */
        if (len == -1) {
            close(fd);
            Logger(PLAINLOG, "", "", hit, 0, threadinfo->logf);
            return NULL;
        }
        Content* cont = GetContentByKey(table, file_name);
        if (cont != NULL)
            __sync_add_and_fetch(&(cont->cacheitem->ref), 1);
        if (cont == NULL) {
            if (len == -1) {
                close(fd);
                Logger(PLAINLOG, "", "", hit, 0, threadinfo->logf);
                return NULL;
            } else {
                cont = malloc(sizeof(Content));
                cont->cacheitem = NULL;
                cont->file_name = file_name;
                cont->length = len;
                cont->pair = NULL;
                cont->address = calloc(len + 10, sizeof(char));
                int file_fd;
                if ((file_fd = open(file_name, O_RDONLY)) == -1) {
                    Error("webserver.c open file 492", "webserver.log");
                    Logger(PLAINLOG, "", "", hit, 0, threadinfo->logf);
                    return NULL;
                }
                if (cont->address == NULL) {
                    free(cont);
                    Logger(PLAINLOG, "", "", hit, 0, threadinfo->logf);
                    return NULL;
                }
                int nread = 0;
                int read_idx = 0;
                do {
                    nread = read(file_fd, cont->address + read_idx,
                        (cont->length - read_idx));
                    read_idx += nread;
                } while (nread != -1 && nread != 0);
                close(file_fd);
            }
        }
        timetmp = TimeDiff(&pre);
        sprintf(
            timeTmpBuffer, "reading request file cost %lf seconds.\n", timetmp);
        strcat(timeDiffLog, timeTmpBuffer);
        cost_counter.cost_time[threadinfo->logf->idx] = timetmp;
        cost_counter.total_cost_time += timetmp;
        if (len != cont->length) {
            char error_buffer[80];
            sprintf(error_buffer,
                "web len!=cont->length\tlen=%ld\tcont->length=%d\t", len,
                cont->length);
            Error(error_buffer, "webserver.log");
        }

        /* make sure write everything*/
        i = 0;
        int nwrite = 0;
        do {
            nwrite = write(fd, (cont->address) + i, (cont->length) - i);
            i += nwrite;

        } while (i < (cont->length));
        if (cont->cacheitem != NULL)
            __sync_sub_and_fetch(&(cont->cacheitem->ref), 1);
        timetmp = TimeDiff(&pre);
        sprintf(timeTmpBuffer,
            "writing request content to socket cost %lf seconds.\n", timetmp);
        strcat(timeDiffLog, timeTmpBuffer);
        cost_counter.cost_time[threadinfo->logf->idx + 1] = timetmp;
        cost_counter.total_cost_time += timetmp;

        (threadinfo->logf->idx) += 2;
        /* usleep(20000); */
        timetmp = TimeDiff(&pre);
        sprintf(timeTmpBuffer,
            "Spleeping for socket to drain "
            "cost %lf seconds.\n",
            timetmp);
        strcat(timeDiffLog, timeTmpBuffer);
        cost_counter.cost_time[threadinfo->logf->idx++] = timetmp;
        cost_counter.total_cost_time += timetmp;
        Logger(LOGTIMEDIFF, timeDiffLog, "", fd, 0, (threadinfo->logf));
        Logger(LOGSPLIT, "Log", "end", hit, 0, (threadinfo->logf));
        Logger(PLAINLOG, "", "", hit, 1, (threadinfo->logf));
        timetmp = TimeDiff(&pre);
        cost_counter.cost_time[threadinfo->logf->idx++] = timetmp;
        threadinfo->logf->idx=0;
        cost_counter.total_cost_time += timetmp;
        /* close(file_fd); */
    }
    close(fd);

#ifdef COUNT_TIME
    pthread_mutex_lock(&(threadinfo->counter->mutex));
    threadinfo->counter->total_cost_time += cost_counter.total_cost_time;
    for (int i = 0; i < COST_TIME_NUM; ++i) {
        threadinfo->counter->cost_time[i] += cost_counter.cost_time[i];
    }
    ++(threadinfo->counter->invokes);
    pthread_mutex_unlock(&(threadinfo->counter->mutex));
#endif

    return NULL;
}
void* LoadFileToHash(void* arg)
{
    LoaderThreadInfo* info = arg;
    char buffer[512];
    int len = 0;
    int i = info->begin_idx;
    int begin_idx = info->begin_idx;
    int j = 0;
    int t = 0;
    int max_cnt = info->max_cnt;
    HashTable* table = info->table;
    LogFile logf;
    LogFileInit(&logf, "webserver.log");
    /*find the nearest valid file name*/
    while (strncmp(file_buffer + i, ".html", 5) != 0)
        ++i;
    i += 5;
    free(info);
    for (t = 0; t < max_cnt; ++t) {
        /* jump over '\n' and '\r' */
        while (!isdigit(file_buffer[i]))
            ++i;
        /* j points to the first number */
        j = i;
        /* find file name end point */
        while (strncmp(file_buffer + i, ".html", 5) != 0)
            ++i;
        i += 5;
        /* now i points to the char after .html */

        len = i - j;
        /* write file name to buffer */
        for (int k = 0; k < len; ++k, ++j) {
            buffer[k] = file_buffer[j];
        }
        buffer[len] = 0;

        int cont_len = GetFileSize(buffer);
        if (GetContentByKey(table, buffer) == NULL) {
            Error("cacheitem==NULL line:528", "webserver.log");
            break;
        }
        if (cachelist->mem_free < cont_len) {
            sprintf(buffer, "load file num:%d\tcahelist_num:%lld\n", t,
                cachelist->num);
            Logger(LOG, buffer, "", getpid(), 1, &logf);
            break;
        }
    }
    sprintf(buffer,
        "loop out: load file,\n begin_idx:%d\n"
        "num:%d\tcahelist_num:%lld\tcachelist->mem_free:%d\n",
        begin_idx, t, cachelist->num, cachelist->mem_free);
    Logger(LOG, buffer, "", getpid(), 1, &logf);
    Logger(LOG, "LoadFileToHash exit", "", getpid(), 1, &logf);
    extern long long hit;
    hit = 0;
    extern long long loss;
    loss = 0;
    return NULL;
}

void* CacheTest(void* data)
{
    long long int old = 0;
    LogFile logf;
    LogFileInit(&logf, "webserver.log");
    char buf[BUFSIZE];
    for (int i = 0; i < 10000; ++i) {
        old = cachelist->num;
        GetContentByKey(table, "0.html");
        if (cachelist->num != old) {
            sprintf(buf, "old:%lld\tnew:%lld\n", old, cachelist->num);
            Logger(LOG, buf, "", getpid(), 1, &logf);
        }
    }
    return NULL;
}

int main(int argc, char** argv)
{
    int i, port, listenfd, hit;
    socklen_t length;
    static struct sockaddr_in cli_addr;
    static struct sockaddr_in serv_addr;
    struct LogFile logf;
    LogFileInit(&logf, "webserver.log");

    if (argc < 3 || argc > 3 || !strcmp(argv[1], "-?")) {
        (void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
                     "\tnweb is a small and very safe mini web server\n"
                     "\tnweb only servers out file/web pages "
                     "with extensions named below\n"
                     "\t and only from the named "
                     "directory or its sub-directories.\n"
                     "\tThere is no fancy features = safe and secure.\n\n"
                     "\tExample:webserver 8181 /home/nwebdir &\n\n"
                     "\tOnly Supports:",
            VERSION);
        for (i = 0; extensions[i].ext != 0; i++)
            (void)printf(" %s", extensions[i].ext);

        (void)printf(
            "\n\tNot Supported: URLs including \"..\", Java, Javascript, CGL\n"
            "\tNot Supported: directories "
            "//etc /bin /lib /tmp /usr /dev /sbin \n"
            "\tNo warranty given or implied\n"
            "\tNigle Griffiths nag@uk.ibm.com\n");
        exit(0);
    }
    if (!strncmp(argv[2], "/", 2) || !strncmp(argv[2], "/etc", 5)
        || !strncmp(argv[2], "/bin", 5) || !strncmp(argv[2], "/lib", 5)
        || !strncmp(argv[2], "/tmp", 5) || !strncmp(argv[2], "/usr", 5)
        || !strncmp(argv[2], "/dev", 5) || !strncmp(argv[2], "/sbin", 6)) {
        (void)printf("ERROR:Bad top directory %s, see nweb -?\n", argv[2]);
        exit(3);
    }
    if (chdir(argv[2]) == -1) {
        (void)printf("ERROR: Can't Change to directory %s\n", argv[2]);
        exit(4);
    }

    /*create HashTable and cachelist*/
    Logger(LOG, "before create table", "", getpid(), 1, &logf);
    table = CreateHashTable(hash_prime);
    if (table == NULL)
        Error("create table error", "webserver.log");
    CacheListInit(&cachelist, MEM_MAX, table);
    file_size = GetFileSize(list_location);
    file_buffer = malloc(sizeof(char) * (file_size) + 1);
    int load_file_fd;
    Logger(LOG, "before read file", "", getpid(), 1, &logf);
    if ((load_file_fd = open(list_location, O_RDONLY)) >= 0) {
        read(load_file_fd, file_buffer, file_size);
    } else {
        Error("load_file_fd open error", "load_error.log");
    }
    close(load_file_fd);
    Logger(LOG, "after close file", "", getpid(), 1, &logf);
    pthread_t** threads = malloc(sizeof(pthread_t*) * LOAD_FILE_THREAD_NUM);
    for (int i = 0, line_num = 0, gap = file_size / LOAD_FILE_THREAD_NUM,
             max_cnt = MAX_FILE_LINE_NUM / LOAD_FILE_THREAD_NUM;
         i < LOAD_FILE_THREAD_NUM; ++i) {
        pthread_t* pth = malloc(sizeof(pthread_t));
        threads[i] = pth;
        LoaderThreadInfo* info
            = (LoaderThreadInfo*)malloc(sizeof(LoaderThreadInfo));
        info->table = table;
        info->begin_idx = line_num;
        info->max_cnt = max_cnt;
        pthread_create(pth, NULL, LoadFileToHash, info);
        line_num += gap;
    }
    Logger(LOG, "before join", "", getpid(), 1, &logf);
    extern long long collide;
    Logger(LOG, "collide:", "", collide, 1, &logf);
    collide = 0;
    for (int t = 0; t < LOAD_FILE_THREAD_NUM; ++t) {
        if (pthread_join(*threads[t], NULL) != 0)
            Error("pthread_join", "webserver.log");
        free(threads[t]);
    }
    Logger(LOG, "after join", "", getpid(), 1, &logf);
    free(file_buffer);
    free(threads);

    Logger(LOG, "after free threads file_buffer", "", getpid(), 1, &logf);

    if (fork() != 0)
        return 0;
    (void)signal(SIGCLD, SIG_IGN);
    (void)signal(SIGHUP, SIG_IGN);
    for (i = 0; i < 32; i++)
        (void)close(i);
    (void)setpgrp();
    Logger(LOG, "nweb starting", argv[1], getpid(), 1, &logf);

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        Logger(ERROR, "system call", "socket", 0, 1, &logf);
    port = atoi(argv[1]);
    if (port < 0 || port > 60000)
        Logger(
            ERROR, "Invalid port number (try 1->60000)", argv[1], 0, 1, &logf);

    Logger(LOG, "before pthread part", "", getpid(), 1, &logf);

    /*pthread part*/
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
#ifdef COUNT_TIME
    /* pthread_t pth; */
    pthread_cond_init(&global_cost_counter_cond, NULL);
    pthread_mutex_init(&global_cost_counter_mutex, NULL);
    /*pthread part end*/

    /*init TimeCounter*/
    memset(&global_cost_counter, 0, sizeof(global_cost_counter));
#endif
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        Logger(ERROR, "system call", "bind", 0, 1, &logf);
        exit(1);
    }
    if (listen(listenfd, 64) < 0)
        Logger(ERROR, "system call", "listen", 0, 0, &logf);
    struct Task* curtask;
    struct ThreadPool* pool = InitThreadPool(THREAD_NUM);

    Logger(LOG, "after init ThreadPool", "", getpid(), 1, &logf);

#ifdef COUNT_TIME
    CostDetail details[COST_TIME_NUM] = { { 0, "RSC",
                                              "Reading socket content" },
        { 0, "VCSCL", "Validation checking for socket content and loging" },
        { 0, "WHSC", "Writing header to socket" },
        { 0, "RRF", "reading requested file" },
        { 0, "WRCS", "writing requested content to socket" },
        { 0, "SLEEP", "Spleeping for socket to drain" },
        { 0, "LOG", "Logging buffer to file" } };
    void* param = malloc(
        sizeof(CostDetail*) + sizeof(ThreadInfo*) + sizeof(ThreadPool*));

    (*(CostDetail**)(param + sizeof(ThreadInfo*))) = (details);
    (*(ThreadPool**)(param + sizeof(ThreadInfo*) + sizeof(CostDetail**)))
        = (pool);
    curtask = (Task*)malloc(sizeof(Task));
    curtask->arg = param;
    curtask->function = (void*)CounterThread;
    if (PushTaskQueue(&(pool->queue), curtask) < 0) {
        Error("counter_thread PushTaskQueue", "webserver.log");
    }

#endif
    if (pool == NULL) {
        Error("InitThreadPool", "webserver.log");
        return 1;
    }
    if (pool->num_threads != THREAD_NUM)
        Error("thread_num", "webserver.log");

    Logger(LOG, "before listen", "", getpid(), 1, &logf);
    /* for (int i = 0; i < 1000; ++i) {
     *     curtask = (Task*)malloc(sizeof(Task));
     *     curtask->arg = malloc(sizeof(ThreadInfo*));
     *     curtask->function = CacheTest;
     *     PushTaskQueue(&(pool->queue), curtask);
     * } */

    for (hit = 1;; hit++) {
        length = sizeof(cli_addr);
        curtask = (Task*)malloc(sizeof(Task));
        curtask->arg = malloc(sizeof(WebParam) + sizeof(ThreadInfo*));
        curtask->function = (void*)Web;
        WebParam* param = ((WebParam*)(curtask->arg + sizeof(ThreadInfo*)));
        if ((param->fd = accept(listenfd, (struct sockaddr*)&cli_addr, &length))
            < 0)
            Logger(ERROR, "system call", "accept", 0, 1, &logf);
        param->hit = hit;

        if (PushTaskQueue(&(pool->queue), curtask) < 0) {
            Error("PushTaskQueue", "webserver.log");
        }
    }
    return 0;
}
