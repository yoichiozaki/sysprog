#ifndef BUILTINS_H 
#define BUILTINS_H

struct builtin {
    char *name; 						// its name, which is used as search index.
    int (*f)(int argc, char *argv[]);	// its content.
};

struct builtin* lookup(char *target_name);
int builtin_cd(int argc, char *argv[]);
int builtin_pwd(int argc, char *argv[]);
int builtin_exit(int argc, char *argv[]);

#endif