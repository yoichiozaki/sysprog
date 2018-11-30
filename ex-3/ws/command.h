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
void print_commands(struct command *head);

#endif