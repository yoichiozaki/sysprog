// 61604239 ozaki yoichi

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
#include <signal.h>
#include "command.h"
#include "builtins.h"
#include "utils.h"

static void welcome(void);
static int prompt(void);
static void init_signal_handlers();
typedef void (*sighandler_t)(int);
sighandler_t trap_signal(int sig, sighandler_t handler);
static void do_nothing(int sig);
static void wait_for_bg();
static char *program_name = "mysh";
extern int amp_flag;

#define SIZE 256
int
main(int argc, char *argv[])
{
    int status;
    init_signal_handlers();
    welcome();
    for (;;) {
        status = prompt();
        continue;
    }
}

#define COMMAND_LINE_INPUT_MAX 2048

static int
prompt(void)
{
    static char buffer[COMMAND_LINE_INPUT_MAX];
    static char p[SIZE];
    struct command *cmd;
    
    amp_flag = 0;
    fprintf(stdout, "\n[%s@%s]\n(mysh)$ ", getlogin(), getcwd(p, SIZE));
    fflush(stdout);
    
    if (fgets(buffer, COMMAND_LINE_INPUT_MAX, stdin) == NULL) {
        exit(0);
    }
    
    cmd = parse(buffer);
    
    if (cmd == NULL) {
        fprintf(stderr, "%s: syntax error\n", program_name);
        return -1;
    }
    
    if (cmd->argc > 0) {
        int status;
        status = execute_commands(cmd);
        return status;
    }
    free_command(cmd);
    return 0;
}

static void
welcome(void)
{
    printf("\t\n");
    printf("\t                             ______  \n");
    printf("\t  _______ ____    ______________/ /_ \n");
    printf("\t ____  __ `__ \\_ / / / /_  ___/_  __ \\\n");
    printf("\t_____  / / / / /  /_/ /_(__  )_  / / /\n");
    printf("\t    /_/ /_/ /_/_\\__, / /____/ /_/ /_/ \n");
    printf("\t               /____/                \n\n");
    // printf("Created ascii logo by http://patorjk.com/software/taag/\n\n");
}

static void 
init_signal_handlers()
{
    trap_signal(SIGINT, do_nothing);
    // trap_signal(SIGCHLD, wait_for_bg);
    trap_signal(SIGCHLD, SIG_IGN);
    trap_signal(SIGTTOU, SIG_IGN);
}

sighandler_t
trap_signal(int sig, sighandler_t handler)
{
    struct sigaction act, old;
    
    act.sa_handler = handler;
    // act.sa_handler = SIG_IGN;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART | SA_SIGINFO;
    if (sigaction(sig, &act, &old) < 0) {
        return NULL;
    }
    printf("\n");
    return old.sa_handler;
}

static void
do_nothing(int sig)
{
    static char p[SIZE];
    fprintf(stdout, "\n[%s@%s]\n(mysh)$ ", getlogin(), getcwd(p, SIZE));
    fflush(stdout);
    return;
}

static void
wait_for_bg()
{
    while(0 >= waitpid(-1, NULL, WNOHANG)) {
        ; // do nothing
    }
    return;
}
