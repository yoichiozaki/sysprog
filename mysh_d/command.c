/* 61604239 */

/* This file is definitions of struct command and functions to execute it for mysh. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>
#include "command.h"
#include "builtins.h"
#include "utils.h"

#define INIT_CAPACITY 8
#define PID_BUILTIN -2

static void execute_pipeline(struct command *head);
static void redirect_to(char *path);
static int wait_pipeline(struct command *head);
static struct command* get_pipeline_tail(struct command *head);
static void print_command_details(struct command *cmd);
int amp_flag = 0;
pid_t head_pid;

// parse input and create command structure.
struct command*
parse(char *p)
{
    struct command *cmd;
    cmd = my_malloc(sizeof(struct command));
    
    // initialize cmd
    cmd->commandID = 0;
    cmd->argc = 0;
    cmd->argv = my_malloc(sizeof(char*) * INIT_CAPACITY);
    cmd->capacity = INIT_CAPACITY;
    cmd->next = NULL;
    
    while (*p) {
        while (*p && isspace((int)*p)) {
            *p++ = '\0';
        }
        
        // if encounter a " ", "|" or ">", exit the while loop
        if (isspace((int)*p) || *p == '|' || *p == '>') {
            break;
        }
        
        if (*p && (!isspace((int)*p) && ((*p) != '|') && ((*p) != '>'))) {
            if (cmd->capacity <= cmd->argc) {
                cmd->capacity *= 2;
                cmd->argv = my_realloc(cmd->argv, cmd->capacity);
            }
            
            // got a token!
            cmd->argv[cmd->argc] = p;
            cmd->argc++;
            if (*p == '&') {
                amp_flag = 1;
                cmd->argc--;
            }
        }
        while (*p && (!isspace((int)*p) && *p != '|' && *p != '>')) {
            p++; // increment p to next token
        }
    }
    
    if (cmd->capacity <= cmd->argc) {
        // reallocate 1 byte for NULL
        cmd->capacity += 1;
        cmd->argv = my_realloc(cmd->argv, cmd->capacity);
    }
    
    cmd->argv[cmd->argc] = NULL;
    if (*p == '|' || *p == '>') {
        if (cmd == NULL || cmd->argc == 0) {
            goto parse_error;
        }
        
        cmd->next = parse(p + 1); // call parse() recursively

        // e.g. ls -l | wc
        if (cmd->next == NULL || cmd->next->argc == 0) {
            goto parse_error;
        }
        
        // e.g. ls -l > output
        if (*p == '>') {
            if (cmd->next->argc != 1) {
                goto parse_error;
            }
            cmd->next->argc = -1;
        }
        *p = '\0';
    }
    struct command *tmp;
    int id = 0;
    for(tmp = cmd; tmp->next != NULL; tmp = tmp->next) {
        tmp->commandID = id;
        id++;
    }
    tmp->commandID = id;
    
    return cmd;
    
parse_error:
    if (cmd) {
        free_command(cmd);
    }
    return NULL;
}

// free the memory allocated to commands.
void
free_command(struct command *cmd)
{
    if (cmd->next != NULL) {
        free_command(cmd->next);
    }
    free(cmd->argv);
    free(cmd);
}

// execute commands.
int
execute_commands(struct command *head)
{
    int status;
    int stdin_ = dup(0);     // save aside original stdin
    int stdout_ = dup(1);    // save aside original stdout
    execute_pipeline(head);
    status = wait_pipeline(head);
    close(0); dup2(stdin_, 0); close(stdin_);
    close(1); dup2(stdout_, 1); close(stdout_);
    tcsetpgrp(STDOUT_FILENO, getpid());
    return status;
}

// helper function execute_pipeline() executes pipelined commands.
static void
execute_pipeline(struct command *head)
{
    struct command *cmd;
    int fds1[2] = {-1, -1}; // previous command's file discriptors
    int fds2[2] = {-1, -1}; // next command's file discriptors
    for (cmd = head; cmd && cmd->argc != -1; cmd = cmd->next) {
        fds1[0] = fds2[0]; // read only
        fds1[1] = fds2[1]; // write only
        if (cmd->next != NULL && cmd->next->argc != -1) {
            if (pipe(fds2) < 0) {
                perror("FATAL EROOR: fail to create pipe");
                exit(3);
            }
        }
        if (lookup(cmd->argv[0]) != NULL) {
            cmd->pid = PID_BUILTIN;
        } else {
            cmd->pid = fork();
            // DEBUG:
            // printf("forked pid: %d\n", cmd->pid);
            if (cmd->pid < 0) { // fail to fork
                perror("FATAL ERROR: fail to fork");
                exit(3);
            }
            if (cmd->pid > 0) { // parent
                if (cmd == head) {
                    head_pid = cmd->pid;
                    // DEBUG:
                    // printf("forked %d\n", head_pid);
                }
                if (fds1[0] != -1) {
                    close(fds1[0]);
                }
                if (fds1[1] != -1) {
                    close(fds1[1]);
                }
                if (!amp_flag) {
                    tcsetpgrp(STDOUT_FILENO, head_pid);
                }
                continue;
            }

            // DEBUG:
            // printf("---------\n");
            // printf("head_pid: %d\n", head_pid);
            // printf("cmd->commandID: %d\n", cmd->commandID);
            // printf("---------\n");
            setpgid(getpid(), head_pid);
        }
        if (cmd != head) {
            close(0); dup2(fds1[0], 0); close(fds1[0]);
            close(fds1[1]);
        }
        
        // cmd->next is not redirect,
        // so get the same output file as the previous command does.
        if (cmd->next != NULL && cmd->next->argc != -1) {
            close(fds2[0]);
            close(1); dup2(fds2[1], 1); close(fds2[1]);
        }
        
        // cmd->next is redirection,
        // so redirect the output.
        if (cmd->next != NULL && cmd->next->argc == -1) {
            redirect_to(cmd->next->argv[0]);
        }
        
        // commands other than builtins is going to be executed.
        if (cmd->pid != PID_BUILTIN) {
            cmd->pid = head_pid + cmd->commandID;
            execvp(cmd->argv[0], cmd->argv);
            // returning from execvp() means failure of its execution.
            fprintf(stderr, "%s: command not found: %s\n", "mysh", cmd->argv[0]);
            exit(1);
        }
    }
}

// helper function redirect_to() sets redirection the output to the file "path"
static void
redirect_to(char *path)
{
    int fd;
    close(1);
    fd = open(path, O_WRONLY|O_TRUNC|O_CREAT, 0666); // open file to redirect
    if (fd < 0) {
        perror(path); // fail to open file
        return;
    }
    if (fd != 1) {
        dup2(fd, 1);   // stdout is piped to file "path"
        close(fd);     // remove the discriptor table's entry of file "path"
    }
}

// wait complition of the pipeline execution and return its status.
static int
wait_pipeline(struct command *head)
{
    if (!amp_flag) {
        struct command *cmd;
        for (cmd = head; cmd && cmd->argc != -1; cmd = cmd->next) {
            if (cmd->pid == PID_BUILTIN) {
                // DEBUG:
                // print_commands(cmd);
                cmd->status = lookup(cmd->argv[0])->f(cmd->argc, cmd->argv); // execute builtin function
            } else {
                // DEBUG;
                // printf("waiting pid %d\n", cmd->pid);
                waitpid(cmd->pid, &cmd->status, 0); // wait complition of the pipeline execution.
            }
        }
        return get_pipeline_tail(head)->status;
    }
    return 999;
}

// helper function get_pipeline_tail() returns the pointer to the last command
static struct command*
get_pipeline_tail(struct command *head)
{
    struct command *cmd;
    for (cmd = head; cmd->next != NULL && cmd->next->argc != -1; cmd = cmd->next) {
        ; // do nothing until reach to the last command.
    }
    return cmd;
}

// FOR DEBUG:
void
print_commands(struct command *head)
{
    struct command *tmp;
    for(tmp = head; tmp->next != NULL; tmp = tmp->next) {
        print_command_details(tmp);
    }
    print_command_details(tmp);
}

static void
print_command_details(struct command *cmd)
{
    int i;
    printf("\n+--[command]----------------------------\n");
    printf("|\tID:\t\t%d\n", cmd->commandID);
    printf("|\targc:\t\t%d\n", cmd->argc);
    for(i = 0; i < cmd->argc; i++) {
        printf("|\targv(%d):\t%s\n", i, *(cmd->argv+i));
    }
    printf("|\tcapacity:\t%d\n", cmd->capacity);
    printf("|\tstatus:\t\t%d\n", cmd->status);
    printf("|\tpid:\t\t%d\n", getpid());
    printf("|\tppid:\t\t%d\n", getppid());
    printf("|\tpgid:\t\t%d\n", getpgid((pid_t)cmd->pid));
    printf("+---------------------------------------\n\n");
}
