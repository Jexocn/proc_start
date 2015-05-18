/*************************************************************************
	> File Name: proc_start.c
	> Author: 
	> Mail: 
	> Created Time: 2015年05月14日 星期四 19时22分56秒
 ************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_PATH 4096

typedef struct {
    char *pwd;
    char *log;
    char *cmd;
    int singlelog;
    int quiet;
}args_t;

typedef struct {
    const args_t *args;
    FILE *fh;
    time_t t;
}log_t;

static void args_release(args_t *args)
{
    if (args->pwd) free(args->pwd);
    if (args->log) free(args->log);
    if (args->cmd) free(args->cmd);
    free(args);
}

static args_t* args_create(int argc, const char *argv[])
{
    args_t *args = NULL;
    int i = 0;
    int parsing_cmd = 0;
    int singlelog = 0;
    int quiet = 0;
    const char *pwd = NULL;
    const char *log = NULL;
    const char **cmd_argv = NULL;
    char *cmd = NULL;
    int cmd_argc = 0;
    size_t cmd_len = 0;

    for(i = 1; i < argc; i++) {
        const char *opt = argv[i];
        if (opt[0] != '-') {
            if (parsing_cmd) {
                cmd_argc++;
                cmd_len += strlen(argv[i]) + 1;
            } else {
                cmd_argv = &argv[i];
                cmd_argc = 1;
                parsing_cmd = 1;
                cmd_len = strlen(argv[i]);
            }
        } else {
            if (strcmp(opt, "-d") == 0) {
                if (i == argc-1) {
                    printf("proc_start: option '-d' requires an argument\n");
                    return NULL;
                }
                pwd = argv[++i];
                parsing_cmd = 0;
            } else if (strcmp(opt, "-l") == 0) {
                if (i == argc-1) {
                    printf("proc_start: option '-l' requires an argument\n");
                    return NULL;
                }
                log = argv[++i];
                parsing_cmd = 0;
            } else if (strcmp(opt, "-s") == 0 || strcmp(opt, "--singlelog") == 0) {
                singlelog = 1;
                parsing_cmd = 0;
            } else if (strcmp(opt, "-q") == 0 || strcmp(opt, "--quiet") == 0) {
                quiet = 1;
                parsing_cmd = 0;
            } else if (strncmp(opt, "--pwd=", strlen("--pwd=")) == 0) {
                if (strlen(opt) == strlen("--pwd=")) {
                    printf("proc_start: option '--pwd' requires an argument\n");
                }
                pwd = &opt[strlen("--pwd=")];
            } else if (strncmp(opt, "--log=", strlen("--log=")) == 0) {
                if (strlen(opt) == strlen("--log=")) {
                    printf("proc_start: option '--log' requires an argument\n");
                    return NULL;
                }
                log = &opt[strlen("--log=")];
            } else if (parsing_cmd) {
                cmd_argc++;
                cmd_len += strlen(argv[i]) + 1;
            } else {
                printf("invalid option %s\n", opt);
                return NULL;
            }
        }
    }

    if (!cmd_argv) {
         printf("proc_start: requires CMD parameter\n");
        return NULL;
    }

    args = calloc(1, sizeof(*args));
    if (pwd) {
        args->pwd = realpath(pwd, NULL);
        if (!args->pwd) {
            i = errno;
            printf("proc_start: get realpath of working directory [%s] fail, errno %d, error [%s]\n",
                pwd, i, strerror(i));
            goto _CHECK_ARGS_ERROR;
        } else {
            struct stat sb;
            if (stat(args->pwd, &sb) == -1) {
                i = errno;
                printf("proc_start: check working directory [%s] state fail, errno %d, error [%s]\n",
                    args->pwd, i, strerror(i));
                goto _CHECK_ARGS_ERROR;
            }
            if (!S_ISDIR(sb.st_mode)) {
                printf("proc_start: working directory [%s] is not directory", args->pwd);
                goto _CHECK_ARGS_ERROR;
            }
        }
    }
    if (log) {
        args->log = malloc(strlen(log));
        strcpy(args->log, log);
    }
    args->singlelog = singlelog;
    args->quiet = quiet;
    args->cmd = malloc(cmd_len+1);
    cmd = args->cmd;
    strcpy(cmd, cmd_argv[0]);
    cmd += strlen(cmd_argv[0]);
    for (i = 1; i < cmd_argc; i++) {
        sprintf(cmd, " %s", cmd_argv[i]);
        cmd += strlen(cmd_argv[i])+1;
    }
    return args;

_CHECK_ARGS_ERROR:
    args_release(args);
    return NULL;
}

static time_t next_day_time(time_t *now)
{
    struct tm * t = localtime(now);
    t->tm_hour = 0;
    t->tm_min = 0;
    t->tm_sec = 0;
    return mktime(t) + 86400;
}

static void log_release(log_t *logger)
{
    if (logger->fh) fclose(logger->fh);
    free(logger);
}

static log_t* log_create(const args_t *args)
{
    FILE *fh = NULL;
    log_t *logger = NULL;
    if (!args->log) return NULL;
    if (args->singlelog) {
        fh = fopen(args->log, "a+");
        if (!fh) {
            printf("proc_start: open log file [%s] fail, errno [%d], error [%s]",
                args->log, errno, strerror(errno));
            return NULL;
        }
        logger = malloc(sizeof(*logger));
        logger->args = args;
        logger->fh = fh;
        logger->t = 0;
        return logger;
    } else {
        char ts[12];
        time_t now = time(NULL);
        char path[MAX_PATH];
        strftime(ts, sizeof(ts), "%F", localtime(&now));
        sprintf(path, "%s.%s", args->log, ts);
        fh = fopen(path, "a+");
        if (!fh) {
            printf("proc_start: open log file [%s] fail, errno [%d], error [%s]",
                path, errno, strerror(errno));
            return NULL;
        }
        logger = malloc(sizeof(*logger));
        logger->args = args;
        logger->fh = fh;
        logger->t = next_day_time(&now);
        return logger;        
    }
    return logger;
}

static void log_print(log_t *logger, const char *s)
{
    time_t now = time(NULL);
    const char *eol = NULL;
    if (logger->args->singlelog || now < logger->t) {
        fprintf(logger->fh, "%s", s);
        return;
    }
    eol = strstr(s, "\n");
    if (!eol) {
        fprintf(logger->fh, "%s", s);
        return;       
    } else {
        char ts[12];
        char path[MAX_PATH];
        FILE *new_fh = NULL;
        fwrite(s, 1, (size_t)(eol-s), logger->fh);
        fflush(logger->fh);
        strftime(ts, sizeof(ts), "%F", localtime(&logger->t));
        sprintf(path, "%s.%s", logger->args->log, ts);
        new_fh = fopen(path, "a+");
        if (!new_fh) {
            printf("proc_start: open new log file [%s] fail, errno [%d], error [%s]",
                path, errno, strerror(errno));
            return;
        }
        fclose(logger->fh);
        logger->fh = new_fh;
        logger->t += 86400;
        fprintf(new_fh, "%s", eol+1);
    }
}

static int start(args_t *args)
{
    log_t *logger = NULL;
    FILE *fp = NULL;
    int ret;
    char buf[2048];
    size_t len;
    if (args->log) {
        logger = log_create(args);
        if (!logger) return -1;
    }
    fp = popen(args->cmd, "r");
    if (!fp) {
        printf("start cmd [%s] fail\n", args->cmd);
        if (logger) log_release(logger);
        return -1;
    }
    ret = fcntl(fileno(fp), F_SETFL, O_NONBLOCK);
    if (ret == -1) {
        ret = errno;
        printf("start fcntl fail, errno [%d], error [%s]\n",
            ret, strerror(ret));
    }
    while (1) {
        len = fread(buf, 1, sizeof(buf)-1, fp);
        if (len > 0) {
            buf[len] = '\0';
            if (logger) log_print(logger, buf);
            if (!args->quiet) printf("%s", buf);
        }
        if (len < sizeof(buf)) {
            if (len == 0 && (feof(fp) || (ferror(fp) && errno != EWOULDBLOCK))) {
                ret = pclose(fp);
                if (ret) {
                    ret = errno;
                    printf("cmd exit, errno [%d], error [%s]\n", ret, strerror(ret));
                }
                if (logger) log_release(logger);
                return 0;
            }
            if (logger) fflush(logger->fh);
            if (!args->quiet) fflush(stdout);
            usleep(30000);
        }
    }
}

static void usage()
{
    printf("SYNOPSIS\n");
    printf("\tproc_start [OPTION]... CMD...\n");
    printf("DESCRIPTION\n");
    printf("\t-d, --pwd=DIRECTORY\n");
    printf("\t\tset progress working directory\n");
    printf("\t-l, --log=FILE\n");
    printf("\t\tset log file name\n");
    printf("\t-s, --singlelog\n");
    printf("\t\tset single log file\n");
    printf("\t-q, --quiet\n");
    printf("\t\tset quiet\n");
}

int main(int argc, const char *argv[])
{
    args_t *args = args_create(argc, argv);
    if (!args) {
        usage();
        return 0;
    }

    if (args->pwd) printf("pwd : %s\n", args->pwd);
    if (args->log) printf("log : %s\n", args->log);
    printf("cmd : %s\n", args->cmd);
    printf("singlelog : %d\n", args->singlelog);

    start(args);
    args_release(args);
    return 0;
}
