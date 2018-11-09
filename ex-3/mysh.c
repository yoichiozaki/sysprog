#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define TOKENLEN 80

#define TKN_NORMAL          1
#define TKN_REDIR_IN        2
#define TKN_REDIR_OUT       3
#define TKN_REDIR_APPEND    4
#define TKN_PIPE            5
#define TKN_BG              6
#define TKN_EOL             7
#define TKN_EOF             8
#define TKN_NONE             -1

struct tkntype {
    int type;
    char *name;
} tkn[] = {
    TKN_NORMAL,         "NORMAL",
    TKN_REDIR_IN,       "REFIR_IN",
    TKN_REDIR_OUT,      "REDIR_OUT",
    TKN_REDIR_APPEND,   "REDIR_APPEND",
    TKN_PIPE,           "PIPE",
    TKN_BG,             "BG",
    TKN_EOL,            "EOL",
    TKN_EOF,            "EOF",
    0,                  NULL,
};

char *pr_ttype(int type);
int gettoken(char *token, int len);

int
main()
{
    char token[TOKENLEN];
    int ttype;

    for(;;) {
        fprintf(stderr, "input: ");
        while((ttype = gettoken(token, TOKENLEN)) != TKN_EOL && ttype != TKN_EOF) {
            printf("\tType: %s, `%s`\n", pr_ttype(ttype), token);
        }
        if (ttype == TKN_EOL) {
            printf("\tType: %s, `%s`\n", pr_ttype(ttype), token);
        } else {
            break;
        }
    }
    putchar('\n');
    return 0;
}

char *
pr_ttype(int type)
{
    struct tkntype *p;
    for (p = tkn; p->name; p++) {
        if (p->type == type) {
            return p->name;
        }
    }
    fprintf(stderr, "Unknown token type: %d\n", type);
    exit(1);
}

int
gettoken(char *token, int len)
{
    int c, i = 0, type = TKN_NONE;
    char *p = token;
    *p = '\0';
    while(isblank(c = getchar())) {
        // skip
    }
    switch (c) {
        case EOF:
            return TKN_EOF;
        case '\n':
            return TKN_EOL;
        case '&':
            return TKN_BG;
        case '|':
            return TKN_PIPE;
        case '<':
            return TKN_REDIR_IN;
        case '>':
            if ((c = getchar()) == '>') {
                return TKN_REDIR_APPEND;
            }
            ungetc(c, stdin);
            return TKN_REDIR_OUT;
    }
    ungetc(c, stdin);
    for (i = 0; i < len - 1; i++) {
        c = getchar();
        if (c != EOF && c != '\n' && c != '&' && c != '<' && c != '>' && c != '|' && !isblank(c)) {
            *p++ = c;
        } else {
            break;
        }
    }
    ungetc(c, stdin);
    *p = '\0';
    if (i > 0) {
        return TKN_NORMAL;
    } else {
        return TKN_NONE;
    }
}
