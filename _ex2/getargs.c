#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BUFLEN  256
#define NARGS   256

void getargs(int *ac, char *av[], char *p);

int
main()
{
    char lbuf[BUFLEN], *argv[NARGS];
    int argc, i;

    for (;;) {
        fprintf(stderr, "input a line: ");
        if (fgets(lbuf, sizeof lbuf, stdin) == NULL) {
            putchar('\n');
            return 0;
        }
        lbuf[strlen(lbuf) - 1] = '\0';
        if (*lbuf == '\0') {
            continue;
        }
        getargs(&argc, argv, lbuf);
        printf("argc = %d\n", argc);
        for (i = 0; i < argc; i++) {
            printf("argv[%d] = '%s'(len = %lu)\n", i, argv[i], strlen(argv[i]));
        }
    }
}

void
getargs(int *ac, char *av[], char *p)
{
    *ac = 0;
    av[0] = NULL;

        for (;;) {
            while (isblank(*p)) {
                p++;
            }
            if (*p == '\0') {
                return;
            }
            av[(*ac)++] = p;
            while (*p && !isblank(*p)) {
                p++;
            }
            if (*p == '\0') {
                return;
            }
            *p++ = '\0';
        }
}

