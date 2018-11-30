/* 61604239 */

/* This file is definitions of struct command and functions to execute it for mysh. */

// struct command represents inputed command instance.
struct command {
    int argc;				// the number of arguments
    char **argv;			// the values of arguments
    int capa; 				// capacity of this command
    int status; 			// command status
    int pid; 				// command pid
    struct command *next; 	// If piped, store a pointer to the next command
};

#define INIT_CAPA 8
#define PID_BUILTIN -2

static struct *command parse(char *p);
static void free_command(struct command *cmd);
static int execute_commands(struct command *head);
static void execute_pipeline(struct command *head);
static void redirect_to(char *path)
static void print_commands(struct command *head);
static int wait_pipeline(struct command *head);
static struct command* get_pipeline_tail(struct command *head);

// parse input and create command structure.
static struct command*
parse(char *p)
{
    struct cmd *command;
    cmd = my_malloc(sizeof(struct command));

    // initialize cmd
    cmd->argc = 0;
    cmd->argv = my_malloc(sizeof(char*) * INIT_CAPA);
    cmd->capa = INIT_CAPA;
    cmd->next = NULL;

    while (*p) {
        while (*p && isspace((int)*p)) {
            *p++ = '\0';
        }

        // if encounter a " ", "|" or ">", exit the while loop
        if (isspace((int)c) || ((c) == '|') || ((c) == '>')) {
            break;
        }

        if (*p && (!isspace((int)*p) && ((*p) != '|') && ((*p) != '>'))) {
            if (cmd->capa <= cmd->argc) {
                cmd->capa *= 2;
                cmd->argv = my_realloc(cmd->argv, cmd->capa);
            }

            // got a token!
            cmd->argv[cmd->argc] = p;
            cmd->argc++;
        }
        while (*p && (!isspace((int)*p) && ((*p) != '|') && ((*p) != '>'))) {
            p++; // increment p to next token
        }
    }
    
    if (cmd->capa <= cmd->argc) {
    	// reallocate 1 byte for NULL
        cmd->capa += 1;
        cmd->argv = xrealloc(cmd->argv, cmd->capa);
    }
    cmd->argv[cmd->argc] = NULL;

    if (*p == '|' || *p == '>') {
        if (cmd == NULL || cmd->argc == 0) {
        	goto parse_error;
        }

        // e.g. ls -l | wc
        cmd->next = parse(p + 1); // call parse() recursively
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
    return cmd;

  parse_error:
    if (cmd) {
    	free_command(cmd);
    }
    return NULL;
}

// free the memory allocated to commands.
static void
free_command(struct command *cmd)
{
    if (cmd->next != NULL) {
        free_command(cmd->next);
    }
    free(cmd->argv);
    free(cmd);
}

// execute commands.
static int
execute_commands(struct command *head)
{
    int status;
    int stdin_ = dup(0);	// save aside original stdin
    int stdout_ = dup(1);	// save aside original stdout
    execute_pipeline(head);
    status = wait_pipeline(head);
    close(0); dup2(stdin_, 0); close(stdin_);
    close(1); dup2(stdout_, 1); close(stdout_);
    return status;
}

// helper function execute_pipeline() executes pipelined commands.
static void
execute_pipeline(struct command *head)
{
    struct command *cmd;
    int fds1[2] = {-1, -1}; // previous command's file discriptors
    int fds2[2] = {-1, -1}; // next command's file discriptors

    for (cmd = head; cmd && !(cmd->argc == -1); cmd = cmd->next) {
        fds1[0] = fds2[0]; // read only
        fds1[1] = fds2[1]; // write only
        if (! (cmd->next == NULL || cmd->next->argc == -1)) {
            if (pipe(fds2) < 0) { 
                perror("FATAL EROOR: fail to create pipe");
                exit(3);
            }
        }
        if (lookup(cmd->argv[0]) != NULL) {
            cmd->pid = PID_BUILTIN;
        } else {
            cmd->pid = fork();
            if (cmd->pid < 0) { // fail to fork
                perror("FATAL ERROR: fail to fork");
                exit(3);
            }
            if (cmd->pid > 0) { // parent
                if (fds1[0] != -1) {
                	close(fds1[0]);
                }
                if (fds1[1] != -1) {
                	close(fds1[1]);
                }
                continue;
            }
        }

        // commands other than head get the same input file
        if (cmd != head) {
            close(0); dup2(fds1[0], 0); close(fds1[0]);
            close(fds1[1]);
        }

        // cmd->next is not redirect,
        // so get the same output file as the previous command has.
        if (! (cmd->next == NULL || cmd->next->argc == -1)) {
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
            execvp(cmd->argv[0], cmd->argv);
            // returning from execvp() means failure of it.
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
        dup2(fd, 1); // stdout is piped to file "path"
        close(fd);	 // remove the discriptor table's entry of file "path"
    }
}

// wait complition of the pipeline execution and return its status.
static int
wait_pipeline(struct command *head)
{
    struct command *cmd;
    for (cmd = head; cmd && cmd->argc != -1; cmd = cmd->next) {
        if (cmd->pid == PID_BUILTIN) {
            cmd->status = lookup(cmd->argv[0])->f(cmd->argc, cmd->argv); // execute builtin function
        } else {
            waitpid(cmd->pid, &cmd->status, 0); // wait complition of the pipeline execution.
        }
    }
    return pipeline_tail(head)->status;
}


// helper function get_pipeline_tail() returns the status of the last command
static struct command*
get_pipeline_tail(struct command *head)
{
    struct cmd *cmd;
    for (cmd = head; cmd->next != NULL || cmd->next->argc != -1; cmd = cmd->next) {
        ; // do nothing until reach to the last command.
    }
    return cmd;
}

static void
print_commands(struct command *head)
{
	int i;
	struct command *tmp;
    for(tmp = head; tmp->next != NULL; tmp = tmp->next) {
        printf("\n+--[command]----------------------------\n");
        printf("|\targc:\t\t%d\n", tmp->argc);
        for(i = 0; i < tmp->argc; i++) {
            printf("|\targv(%d):\t%s\n", i, *(tmp->argv+i));
        }
        printf("|\tcapa:\t\t%d\n", tmp->capa);
        printf("|\tstatus:\t\t%d\n", tmp->status);
        printf("|\tpid:\t\t%d\n", tmp->pid);
        printf("+---------------------------------------\n\n");
    }
    printf("\n+--[command]----------------------------\n");
    printf("|\targc:\t\t%d\n", tmp->argc);
    for(i = 0; i < tmp->argc; i++) {
        printf("|\targv(%d):\t%s\n", i, *(tmp->argv+i));
    }
    printf("|\tcapa:\t\t%d\n", tmp->capa);
    printf("|\tstatus:\t\t%d\n", tmp->status);
    printf("|\tpid:\t\t%d\n", tmp->pid);
    printf("+---------------------------------------\n\n");
}