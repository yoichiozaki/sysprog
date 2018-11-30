#ifndef COMMAND_H
#define COMMAND_H

struct command {
    int argc;				// the number of arguments
    char **argv;			// the values of arguments
    int capacity; 			// capacitycity of this command
    int status; 			// command status
    int pid; 				// command pid
    struct command *next; 	// If piped, store a pointer to the next command
};

struct command* parse(char *p);
void free_command(struct command *cmd);
int execute_commands(struct command *head);
// extern void execute_pipeline(struct command *head);
// extern void redirect_to(char *path);
void print_commands(struct command *head);
// extern int wait_pipeline(struct command *head);
// struct command* get_pipeline_tail(struct command *head);

#endif