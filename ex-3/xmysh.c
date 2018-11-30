/* 61604239 */
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

static void welcome();
static void prompt();

// command
extern struct *command parse(char *p);
extern void free_command(struct command *cmd);
extern int execute_commands(struct command *head);
extern void execute_pipeline(struct command *head);
extern void redirect_to(char *path)
extern void print_commands(struct command *head);
extern int wait_pipeline(struct command *head);
extern struct command* get_pipeline_tail(struct command *head);

// builtins
extern struct builtin* lookup(char *target_name);
extern int builtin_cd(int argc, char *argv[]);
extern int builtin_pwd(int argc, char *argv[]);
extern int builtin_exit(int argc, char *argv[]);

// utils
extern void* my_malloc(size_t sz);
extern void* my_realloc(void *ptr, size_t sz);

static char *program_name = "mysh";

int
main(int argc, char *argv[])
{
    for (;;) {
        prompt();
    }
    exit(0);
}

#define COMMAND_LINE_INPUT_MAX 2048

static void
prompt(void)
{
    static char buffer[COMMAND_LINE_INPUT_MAX]; // myshの入力コマンドを貯めておくバッファ
    struct command *cmd; // 実行対象のコマンドを表すcmd型の変数

    fprintf(stdout, "(mysh) >> "); // プロンプトの表示
    fflush(stdout);

    if (fgets(buffer, COMMAND_LINE_INPUT_MAX, stdin) == NULL) // bufに入力されたコマンドを格納
        exit(0); // 失敗したらexit

    cmd = parse(buffer); // 文字列からcmd構造体を構成して返す

    if (cmd == NULL) {
        fprintf(stderr, "%s: syntax error\n", program_name);
        return;
    }

    if (cmd->argc > 0) {
        execute_commands(cmd);
    }

    free_cmd(cmd);
}

static void
welcome()
{
    printf("\t\n");
    printf("\t                          ______  \n");
    printf("\t_______ ________  ___________  /_ \n");
    printf("\t__  __ `__ \\_  / / /_  ___/_  __ \\\n");
    printf("\t_  / / / / /  /_/ /_(__  )_  / / /\n");
    printf("\t/_/ /_/ /_/_\\__, / /____/ /_/ /_/ \n");
    printf("\t           /____/                 \n");
    printf("\t\n");
}