#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)

#define OP_CREATE "create"
#define OP_START "start"
#define OP_EXEC "exec"
#define OP_STOP "stop"
#define OP_PAUSE "pause"

#define FLAG_INIT_PROCESS_EXISTS (1 << 0)


#define STR_EQUAL(s1, s2) (strcmp((s1), (s2)) == 0)

enum OP_TYPE {
    CREATE,
    START,
    EXEC,
    STOP,
    PAUSE
};

typedef struct {
    enum OP_TYPE op;
    char **argv;
    int *pipe;
    key_t *queue;
} args_t;

static char container_process_stack[STACK_SIZE];

void print_e(char *e)
{
    fprintf(stderr, "Error %s: %s\n", e, strerror(errno));
}

int init(void *args)
{
    unsigned init_process_exist = 0;
    char **command;

    if (mount("proc", "/proc", "proc", 0, "") != 0) {
        print_e("failed to mount proc in container");
        return 1;
    }
    char *hostname = "init";
    if (sethostname(hostname, strlen(hostname)) != 0) {
        print_e("failed to set hostname in container");
        return 1;
    }
    args_t *a = args;
    for (;;) {
        switch (a->op) {
        case CREATE:
            if (init_process_exist & FLAG_INIT_PROCESS_EXISTS) {
                print_e("container init process already exists");
                return 1;
            }
            init_process_exist |= FLAG_INIT_PROCESS_EXISTS;
            command = a->argv;
            break;
        case START:
            if (!(init_process_exist & FLAG_INIT_PROCESS_EXISTS)) {
                print_e("no init process exists to be start. first create one and then start it");
                return 1;
            }
            if (execvp(command[0], command) == -1) {
                print_e("failed to start init process");
                return 1;
            }
            printf("started init process....\n");
        case EXEC:
        case STOP:
        case PAUSE:
          break;
        }
    }
}


// rc create -n con
// rc run -p /bin/sh
// rc exec -n con px aux
int main(int argc, char**argv)
{
    int pipe_fd[2];

    if (argc < 2) {
        fprintf(stderr, "no command specified\n");
    }
    args_t a = {0};
    if (STR_EQUAL(argv[1], OP_CREATE)) {
        a.op = CREATE;
    } else if (STR_EQUAL(argv[1], OP_START)) {
        a.op = START;
    } else if (STR_EQUAL(argv[1], OP_EXEC)) {
        a.op = EXEC;
    } else if (STR_EQUAL(argv[1], OP_STOP)) {
        a.op = STOP;
    } else if (STR_EQUAL(argv[1], OP_PAUSE)) {
        a.op = PAUSE;
    } else {
        print_e("operation not recognized");
        return 1;
    }
    a.argv = &argv[2];
    if (pipe(pipe_fd) == -1) {
        print_e("failed to create pipe");
        return 1;
    }
    a.pipe = pipe_fd;
    // skip ipc namespace for communication
    int flags = CLONE_NEWNET | CLONE_NEWUTS | CLONE_NEWNS |
        CLONE_NEWPID | CLONE_NEWUSER | SIGCHLD;
    pid_t pid = clone(init, container_process_stack + STACK_SIZE,
            flags, &a);
    if (pid < 0) {
        print_e("failed to initilize container at clone stage");
        return 1;
    }
    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        print_e("failed to wait for init process");
        return 1;
    }
    if (WIFEXITED(status)) {
        char b[50] = {0};
        sprintf(b, "init process exited with status %d", WEXITSTATUS(status));
        print_e(b);
    }
    return 0;
}

