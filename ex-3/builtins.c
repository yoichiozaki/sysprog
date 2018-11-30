/* 61604239 */

/* This file is definitions of builtin functions for mysh. */

static struct builtin* lookup(char *target_name);
static int builtin_cd(int argc, char *argv[]);
static int builtin_pwd(int argc, char *argv[]);
static int builtin_exit(int argc, char *argv[]);

// struct builtin represents a builtin function of mysh.
struct builtin {
    char *name; 						// its name, which is used as search index.
    int (*f)(int argc, char *argv[]);	// its content.
};

struct builtin builtins[] = {
    {"cd",      builtin_cd},	// cd stands for Change Directory.
    {"pwd",     builtin_pwd},	// pwd stands for Print Working Directory.
    {"exit",    builtin_exit},
    {NULL,      NULL}			// tell the end of list.
};

// lookup() takes the name of the function as an argument, 
// and returns the pointer if it is registered as a built-in function.
static struct builtin*
lookup(char *target_name)
{
    struct builtin *p;

    for (p = builtins; p->name; p++) {
        if (strcmp(target_name, p->name) == 0)
            return p; // got it.
    }
    return NULL;
}

// cd has to be a builtin because the shell itself needs to change 
// its "cwd" - current working directory - not a sub-process.
// If we write a program that calls the chdir() system call 
// to change its current working directory to some new directory, then exits, 
// we'll quickly find out that our shell - the parent process of that program when it runs - 
// is still in the original directory where we started from.
static int
builtin_cd(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "%s: wrong argument\n", argv[0]);
        return 1;
    }
    if (chdir(argv[1]) < 0) {
        perror(argv[1]);
        return 1;
    }
    return 0;
}

static int
builtin_pwd(int argc, char *argv[])
{
    char buf[PATH_MAX];

    if (argc != 1) {
        fprintf(stderr, "%s: wrong argument\n", argv[0]);
        return 1;
    }
    if (!getcwd(buf, PATH_MAX)) {
        fprintf(stderr, "%s: cannot get working directory\n", argv[0]);
        return 1;
    }
    printf("%s\n", buf);
    return 0;
}

static int
builtin_exit(int argc, char *argv[])
{
    if (argc != 1) {
        fprintf(stderr, "%s: too many arguments\n", argv[0]);
        return 1;
    }
    exit(0);
}
