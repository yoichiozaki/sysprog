#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>

#define NHASH 4

#define STAT_LOCKED 0x00000001
#define STAT_VALID  0x00000002
#define STAT_DWR    0x00000004
#define STAT_KRDWR  0x00000008
#define STAT_WAITED 0x00000010
#define STAT_OLD    0x00000020

/* deta structure */
struct buf_header
{
    int bufno;
    int blkno;
    struct buf_header *hash_fp;
    struct buf_header *hash_bp;
    struct buf_header *free_fp;
    struct buf_header *free_bp;
    unsigned int stat;
    char *cache_data;
};
struct buf_header hash_head[4];
struct buf_header free_head;
struct buf_header buf0, buf1, buf2, buf3, buf4, buf5, buf6, buf7, buf8, buf9, buf10, buf11;
char result[7];
int senario;
enum Commands {
    HELP,
    INIT,
    FREE,
    BUF,
    HASH,
    GETBLK,
    BRELSE,
    SET,
    RESET,
    QUIT,
    FEMPTY
};

/* function prototypes */
void getargs(int *ac, char *av[], char *p);
void show_help();
void init();
void show_free_list();
void show_all_buf_status();
void show_buf_status(int bufno);
void show_all_hash_list();
void show_hash_list();
struct buf_header *getblk(int blkno);
void brelse(int blkno);
void set(int n, char *stats[]);
void reset(int n, char *stats[]);
int input2command(char *input);
void show_welcome_message();
void initial_condition();
struct buf_header *search_hash(int blkno);
int is_any_free_buffer();
void insert_head(struct buf_header *h, struct buf_header *p);
void insert_tail(struct buf_header *h, struct buf_header *p);
void insert_head_free(struct buf_header *h, struct buf_header *p);
void insert_tail_free(struct buf_header *h, struct buf_header *p);
void remove_from_hash(struct buf_header *p);
void remove_from_free(struct buf_header *p);
char *convert_status2letter(struct buf_header *p);
unsigned int convert_letter2status(char p);
void make_free_list_empty();;

#define BUFLEN  256
#define NARGS   256

int
main()
{
    enum Commands command;
    char lbuf[BUFLEN], *argv[NARGS];
    int argc, i;
    show_welcome_message();
    init();
    for (;;) {
        fprintf(stderr, "$ ");
        if (fgets(lbuf, sizeof lbuf, stdin) == NULL) {
            putchar('\n');
            return 0;
        }
        lbuf[strlen(lbuf) - 1] = '\0';
        if (*lbuf == '\0') {
            continue;
        }
        getargs(&argc, argv, lbuf);

        // printf("-----[DEBIG]-----\n");
        // printf("argc = %d\n", argc);
        // for (i = 0; i < argc; i++) {
        //     printf("argv[%d] = '%s'(len = %lu)\n", i, argv[i], strlen(argv[i]));
        // }
        // printf("-----[DEBUG]-----\n");

        switch (input2command(argv[0])) {
            case HELP:
                show_help();
                continue;
            case INIT:
                init();
                continue;
            case FREE:
                show_free_list();
                continue;
            case BUF:
                if (argc == 1) {
                    int j;
                    show_all_buf_status();
                    continue;
                }
                int k;
                for (k = 1; k < argc; k++) {
                    int blkno = atoi(argv[k]);
                    show_buf_status(blkno);
                }
                continue;
            case HASH:
                if (argc > 5) {
                    fprintf(stderr, "Error: too many arguments.\nUsage: hash [n...(up to 4 arguments)]\tn = 0, 1, 2, 3\n");
                    continue;
                }
                if (argc == 1) {
                    int j;
                    show_all_hash_list();
                    continue;
                }
                for (k = 1; k < argc; k++) {
                    int hash_value = atoi(argv[k]);
                    show_hash_list(hash_value);
                }
                continue;
            case GETBLK:
                if (argc == 2 && isdigit(*argv[1])) {
                    int n = atoi(argv[1]);
                    getblk(n);
                    continue;
                } else {
                    fprintf(stderr, "Usage: %s <number>\n", argv[0]);
                    continue;
                }
            case BRELSE:
                if (argc == 2 && isdigit(*argv[1])) {
                    int n = atoi(argv[1]);
                    brelse(n);
                    continue;
                } else {
                    fprintf(stderr, "Usage: %s <number>\n", argv[0]);
                    continue;
                }
            case SET:
                if (argc < 3) {
                    fprintf(stderr, "Error: missing some arguments.\n Usage: set n stat [stat...]\tstat is L/V/D/K/W/O.\n");
                    continue;
                }
                set(argc, argv);
                continue;
            case RESET:
                if (argc < 3) {
                    fprintf(stderr, "Error: missing some arguments.\n Usage: reset n stat [stat...]\tstat is L/V/D/K/W/O.\n");
                }
                reset(argc, argv);
                continue;
            case QUIT:
                fprintf(stderr, "Bye Bye\n");
                exit(0);
            case FEMPTY:
                make_free_list_empty();
                continue;
            default:
                fprintf(stderr, "No such command: %s\n", argv[0]);
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

void
show_help()
{
    printf("Below is list of commands for this buffer cache emulator.\n");
    printf("\n");
    printf("+------------------------+------------------------------------------------------------------------------------------------------------------------------------------+\n");
    printf("|        Command         |                                                               Description                                                                |\n");
    printf("+------------------------+------------------------------------------------------------------------------------------------------------------------------------------+\n");
    printf("| help                   | show help for commands.                                                                                                                  |\n");
    printf("| init                   | Initialize hash list and free list as below.                                                                                             |\n");
    printf("| buf [n...]             | If there is no argument, display the status of all buffers. When the argument n is given, display the state of the buffer number n.      |\n");
    printf("| hash [n...]            | If there is no argument, display hash list of all buffers. When the argument n is given, display the hash list with its hash value of n. |\n");
    printf("| free                   | Display free list.                                                                                                                       |\n");
    printf("| getblk n               | Execute getblk(n).                                                                                                                       |\n");
    printf("| brelse n               | Executes brelse(bp). bp is pointing to the logical block number n's buffer header.                                                       |\n");
    printf("| set n stat [stat...]   | Set the state stat for the buffer of logical block number n. stat is represented in alphabetical letters as follows.                     |\n");
    printf("|                        |   L: STAT_LOCKED (locked)                                                                                                                |\n");
    printf("|                        |   V: STAT_VALID (contain valid data)                                                                                                     |\n");
    printf("|                        |   D: STAT_DWR (delayed write)                                                                                                            |\n");
    printf("|                        |   K: STAT_KRDWR (kernel read/write)                                                                                                      |\n");
    printf("|                        |   W: STAT_WAITED (process is waiting)                                                                                                    |\n");
    printf("|                        |   O: STAT_OLD (data is old)                                                                                                              |\n");
    printf("| reset n stat [stat...] | Reset the state stat for the buffer of logical block number n.                                                                           |\n");
    printf("| quit                   | Quit the Buffer Cache Emulator.                                                                                                          |\n");
    printf("+------------------------+------------------------------------------------------------------------------------------------------------------------------------------+\n");
    printf("\n");

    initial_condition();
}

void
init()
{
    hash_head[0].blkno = -1;
    hash_head[1].blkno = -1;
    hash_head[2].blkno = -1;
    hash_head[3].blkno = -1;

    buf0.blkno = 28;
    buf1.blkno = 4;
    buf2.blkno = 64;
    buf3.blkno = 17;
    buf4.blkno = 5;
    buf5.blkno = 97;
    buf6.blkno = 98;
    buf7.blkno = 50;
    buf8.blkno = 10;
    buf9.blkno = 3;
    buf10.blkno = 35;
    buf11.blkno = 99;

    hash_head[0].hash_fp = &buf0;
    buf0.hash_fp = &buf1;
    buf1.hash_fp = &buf2;
    buf2.hash_fp = &hash_head[0];
    hash_head[1].hash_fp = &buf3;
    buf3.hash_fp = &buf4;
    buf4.hash_fp = &buf5;
    buf5.hash_fp = &hash_head[1];
    hash_head[2].hash_fp = &buf6;
    buf6.hash_fp = &buf7;
    buf7.hash_fp = &buf8;
    buf8.hash_fp = &hash_head[2];
    hash_head[3].hash_fp = &buf9;
    buf9.hash_fp = &buf10;
    buf10.hash_fp = &buf11;
    buf11.hash_fp = &hash_head[3];
    
    hash_head[0].hash_bp = &buf2;
    buf2.hash_bp = &buf1;
    buf1.hash_bp = &buf0;
    buf0.hash_bp = &hash_head[0];
    hash_head[1].hash_bp = &buf5;
    buf5.hash_bp = &buf4;
    buf4.hash_bp = &buf3;
    buf3.hash_bp = &hash_head[1];
    hash_head[2].hash_bp = &buf8;
    buf8.hash_bp = &buf7;
    buf7.hash_bp = &buf6;
    buf6.hash_bp = &hash_head[2];
    hash_head[3].hash_bp = &buf11;
    buf11.hash_bp = &buf10;
    buf10.hash_bp = &buf9;
    buf9.hash_bp = &hash_head[3];

    int i;
    for (i = 0; i < NHASH; i++) {
        struct buf_header *p;
        int j = 0;
        for (p = hash_head[i].hash_fp; p != &hash_head[i]; p = p->hash_fp) {
            p->bufno = 3*i + j;
            j++;
        }
    }

    free_head.free_fp = &buf9;
    buf9.free_fp = &buf4;
    buf4.free_fp = &buf1;
    buf1.free_fp = &buf0;
    buf0.free_fp = &buf5;
    buf5.free_fp = &buf8;
    buf8.free_fp = &free_head;
    free_head.free_bp = &buf8;
    buf8.free_bp = &buf5;
    buf5.free_bp = &buf0;
    buf0.free_bp = &buf1;
    buf1.free_bp = &buf4;
    buf4.free_bp = &buf9;
    buf9.free_bp = &free_head; 

    // all reset
    buf0.stat &= ~STAT_LOCKED;
    buf1.stat &= ~STAT_LOCKED;
    buf2.stat &= ~STAT_LOCKED;
    buf3.stat &= ~STAT_LOCKED;
    buf4.stat &= ~STAT_LOCKED;
    buf5.stat &= ~STAT_LOCKED;
    buf6.stat &= ~STAT_LOCKED;
    buf7.stat &= ~STAT_LOCKED;
    buf8.stat &= ~STAT_LOCKED;
    buf9.stat &= ~STAT_LOCKED;
    buf10.stat &= ~STAT_LOCKED;
    buf11.stat &= ~STAT_LOCKED;

    buf0.stat &= ~STAT_VALID;
    buf1.stat &= ~STAT_VALID;
    buf2.stat &= ~STAT_VALID;
    buf3.stat &= ~STAT_VALID;
    buf4.stat &= ~STAT_VALID;
    buf5.stat &= ~STAT_VALID;
    buf6.stat &= ~STAT_VALID;
    buf7.stat &= ~STAT_VALID;
    buf8.stat &= ~STAT_VALID;
    buf9.stat &= ~STAT_VALID;
    buf10.stat &= ~STAT_VALID;
    buf11.stat &= ~STAT_VALID;

    buf0.stat &= ~STAT_DWR;
    buf1.stat &= ~STAT_DWR;
    buf2.stat &= ~STAT_DWR;
    buf3.stat &= ~STAT_DWR;
    buf4.stat &= ~STAT_DWR;
    buf5.stat &= ~STAT_DWR;
    buf6.stat &= ~STAT_DWR;
    buf7.stat &= ~STAT_DWR;
    buf8.stat &= ~STAT_DWR;
    buf9.stat &= ~STAT_DWR;
    buf10.stat &= ~STAT_DWR;
    buf11.stat &= ~STAT_DWR;

    buf0.stat &= ~STAT_KRDWR;
    buf1.stat &= ~STAT_KRDWR;
    buf2.stat &= ~STAT_KRDWR;
    buf3.stat &= ~STAT_KRDWR;
    buf4.stat &= ~STAT_KRDWR;
    buf5.stat &= ~STAT_KRDWR;
    buf6.stat &= ~STAT_KRDWR;
    buf7.stat &= ~STAT_KRDWR;
    buf8.stat &= ~STAT_KRDWR;
    buf9.stat &= ~STAT_KRDWR;
    buf10.stat &= ~STAT_KRDWR;
    buf11.stat &= ~STAT_KRDWR;

    buf0.stat &= ~STAT_WAITED;
    buf1.stat &= ~STAT_WAITED;
    buf2.stat &= ~STAT_WAITED;
    buf3.stat &= ~STAT_WAITED;
    buf4.stat &= ~STAT_WAITED;
    buf5.stat &= ~STAT_WAITED;
    buf6.stat &= ~STAT_WAITED;
    buf7.stat &= ~STAT_WAITED;
    buf8.stat &= ~STAT_WAITED;
    buf9.stat &= ~STAT_WAITED;
    buf10.stat &= ~STAT_WAITED;
    buf11.stat &= ~STAT_WAITED;

    buf0.stat &= ~STAT_OLD;
    buf1.stat &= ~STAT_OLD;
    buf2.stat &= ~STAT_OLD;
    buf3.stat &= ~STAT_OLD;
    buf4.stat &= ~STAT_OLD;
    buf5.stat &= ~STAT_OLD;
    buf6.stat &= ~STAT_OLD;
    buf7.stat &= ~STAT_OLD;
    buf8.stat &= ~STAT_OLD;
    buf9.stat &= ~STAT_OLD;
    buf10.stat &= ~STAT_OLD;
    buf11.stat &= ~STAT_OLD;

    // set status for needed
    buf0.stat |= STAT_VALID;
    buf1.stat |= STAT_VALID;
    buf2.stat |= STAT_VALID;
    buf3.stat |= STAT_VALID;
    buf4.stat |= STAT_VALID;
    buf5.stat |= STAT_VALID;
    buf6.stat |= STAT_VALID;
    buf7.stat |= STAT_VALID;
    buf8.stat |= STAT_VALID;
    buf9.stat |= STAT_VALID;
    buf10.stat |= STAT_VALID;
    buf11.stat |= STAT_VALID;
    buf2.stat |= STAT_LOCKED;
    buf3.stat |= STAT_LOCKED;
    buf6.stat |= STAT_LOCKED;
    buf7.stat |= STAT_LOCKED;
    buf10.stat |= STAT_LOCKED;
    buf11.stat |= STAT_LOCKED;

    senario = 0;

    printf("Initialized.\n");

}

void
show_free_list()
{
    struct buf_header *p;
    for (p = free_head.free_fp; p!= &free_head; p = p->free_fp) {
        printf("[%2d: %2d %s] ", p->bufno, p->blkno, convert_status2letter(p));
    }
    printf("\n");
}

void
show_all_buf_status()
{
    int i;
    struct buf_header *p;
    for (i = 0; i < 3; i++) {
        for (p = hash_head[i].hash_fp; p != &hash_head[i]; p = p->hash_fp) {
            printf("[%2d: %2d %s]\n", p->bufno, p->blkno, convert_status2letter(p));
        }
    }
}

void
show_buf_status(int bufno)
{
    struct buf_header *p;
    p = search_hash(bufno);
    if (p == NULL) {
        fprintf(stderr, "There is no buffer which cointain bufno %d's data.\n", bufno);
        return;
    }
    printf("[%2d: %2d %s]\n", p->bufno, p->blkno, convert_status2letter(p));
}

void
show_all_hash_list()
{
    int i;
    struct buf_header *p;
    for (i = 0; i < 4; i++) {
        printf("%d: ", i);
        for (p = hash_head[i].hash_fp; p != &hash_head[i]; p = p->hash_fp) {
            printf("[%2d: %2d %s] ", p->bufno, p->blkno, convert_status2letter(p));
        }
        printf("\n");
    }
}

void
show_hash_list(int hash_value)
{
    if (hash_value == 0 || hash_value == 1 || hash_value == 2 || hash_value == 3) {
        printf("%d: ", hash_value);
        struct buf_header *p;
        for (p = hash_head[hash_value].hash_fp; p != &hash_head[hash_value]; p = p->hash_fp) {
            printf("[%2d: %2d %s] ", p->bufno, p->blkno, convert_status2letter(p));
        }
        printf("\n");
        return;
    } else {
        fprintf(stderr, "Error: invalid hash value %d. Please retry with 0, 1, 2, or 3\n", hash_value);
        return;
    }
}

//TODO: シナリオ３の動作確認

struct buf_header *
getblk(int blkno)
{
    int flag = 1;
    struct buf_header *p;
    while (flag) {
        if ((p = search_hash(blkno)) != NULL)  {
            if ((p->stat & STAT_LOCKED) == STAT_LOCKED) {
                // SENARIO 5
                // sleep();
                printf("Process goes to sleep.\n");
                p->stat |= STAT_WAITED;
                // continue;
                return NULL;
            }
            // SENARIO 1
            p->stat |= STAT_LOCKED;
            remove_from_free(p);
            printf("Return a pointer to blkno %d\n", p->blkno);
            return p;
        } else {
            if (!is_any_free_buffer()) {
                // SENARIO 4
                // sleep();
                printf("Process goes to sleep until a event which makes any buffer free.\n");
                senario = 4;
                // continue;
                return NULL;
            }
            struct buf_header *q = free_head.free_fp;
            remove_from_free(free_head.free_fp);
            if ((q->stat & STAT_DWR) == STAT_DWR) {
                // SENARIO 3
                // asynchronous write buffer to disk;
                q->stat &= ~STAT_DWR;
                q->stat |= STAT_LOCKED;
                q->stat |= STAT_KRDWR;
                printf("Asynchronous write occured.\n");
                q->stat |= STAT_OLD;
                continue;
            }
            // SENARIO 2
            remove_from_hash(q);
            q->blkno = blkno;
            insert_tail(&hash_head[blkno%4], q);
            q->stat |= STAT_LOCKED;
            q->stat &= ~STAT_VALID;
            printf("Return a pointer to blkno %d\n", q->blkno);
            return q;
        }
    }
    return NULL;
}

void
brelse(int blkno)
{
    struct buf_header *p = search_hash(blkno);
    if (p == NULL) {
        fprintf(stderr, "There is no buffer whose number is %d\n", blkno);
        return;
    }
    if ((p->stat & STAT_LOCKED) != STAT_LOCKED) {
        fprintf(stderr, "The buffer(blkno = %d) is not locked.\n", blkno);
        return;
    }

    if (senario == 4) {
        // wakeup();
        printf("Wakeup processes waiting for any buffer.\n");
    } else {
        // wakeup();
        printf("Wakeup processes waiting for buffer of blkno %d\n", blkno);
    }

    if ((p->stat & STAT_VALID) == STAT_VALID && (p->stat & STAT_OLD) != STAT_OLD) {
        // enqueue buffer at end of free list;
        insert_tail_free(&free_head, p);
        p->stat &= ~STAT_WAITED;
    } else {
        // enqueue buffer at beginning of free list;
        insert_head_free(&free_head, p);
        p->stat &= ~STAT_OLD;
    }
    // lower_cpu_level();
    p->stat &= ~STAT_LOCKED;
}

void
set(int n, char *stats[])
{
    char before[7];
    int m = atoi(stats[1]);
    struct buf_header *p = search_hash(m);
    if (p == NULL) {
        fprintf(stderr, "There is no buffer which cointain bufno %d's data.\n", m);
        return;
    }
    strcpy(before, convert_status2letter(p));
    int i;
    for (i = 2; i < n; i++) {
        if (strcasecmp(stats[i], "L") || strcasecmp(stats[i], "V") || strcasecmp(stats[i], "D") || strcasecmp(stats[i], "K") || strcasecmp(stats[i], "W") || strcasecmp(stats[i], "O")) {
            p->stat |= convert_letter2status(*stats[i]);
        } else {
            fprintf(stderr, "Error: Unexpected stat symbol %s\n", stats[i]);
            return;
        }
    }
    printf("buffer %d's status changed.\n", m);
    printf("[BEFORE] %s -> [AFTER]: %s\n", before, convert_status2letter(p));
}

void
reset(int n, char *stats[])
{
    char before[7];
    int m = atoi(stats[1]);
    struct buf_header *p = search_hash(m);
    if (p == NULL) {
        if (m == 0) {
            fprintf(stderr, "Error: invalid input.\nUsage: reset <blkno> [stat ...]");
            return;
        }
        fprintf(stderr, "There is no buffer which cointain bufno %d's data.\n", m);
        return;
    }
    strcpy(before, convert_status2letter(p));
    int i;
    for (i = 2; i < n; i++) {
        if (strcasecmp(stats[i], "L") || strcasecmp(stats[i], "V") || strcasecmp(stats[i], "D") || strcasecmp(stats[i], "K") || strcasecmp(stats[i], "W") || strcasecmp(stats[i], "O")) {
            p->stat &= ~convert_letter2status(*stats[i]);
        } else {
            fprintf(stderr, "Error: Unexpected stat symbol: %s\n", stats[i]);
            return;
        }
    }
    printf("buffer %d's status changed.\n", m);
    printf("[BEFORE] %s -> [AFTER]: %s\n", before, convert_status2letter(p));
}

int
input2command(char *input)
{
    if (strcasecmp(input, "help") == 0) {
        return HELP;
    } else if (strcasecmp(input, "init") == 0) {
        return INIT;
    } else if (strcasecmp(input, "free") == 0) {
        return FREE;
    } else if (strcasecmp(input, "buf") == 0) {
        return BUF;
    } else if (strcasecmp(input, "hash") == 0) {
        return HASH;
    } else if (strcasecmp(input, "getblk") == 0) {
        return GETBLK;
    } else if (strcasecmp(input, "brelse") == 0) {
        return BRELSE;
    } else if (strcasecmp(input, "set") == 0) {
        return SET;
    } else if (strcasecmp(input, "reset") == 0) {
        return RESET;
    } else if (strcasecmp(input, "quit") == 0) {
        return QUIT;
    } else if (strcasecmp(input, "fempty") == 0) {
        return FEMPTY;
    } else {
        return 999;
    }
}

void
show_welcome_message()
{
    printf(" ____         __  __              ____           _            _____                 _       _               \n");
    printf("| __ ) _   _ / _|/ _| ___ _ __   / ___|__ _  ___| |__   ___  | ____|_ __ ___  _   _| | __ _| |_ ___  _ __   \n");
    printf("|  _ \\| | | | |_| |_ / _ \\ '__| | |   / _` |/ __| '_ \\ / _ \\ |  _| | '_ ` _ \\| | | | |/ _` | __/ _ \\| '__|  \n");
    printf("| |_) | |_| |  _|  _|  __/ |    | |__| (_| | (__| | | |  __/ | |___| | | | | | |_| | | (_| | || (_) | |     \n");
    printf("|____/ \\__,_|_| |_|  \\___|_|     \\____\\__,_|\\___|_| |_|\\___| |_____|_| |_| |_|\\__,_|_|\\__,_|\\__\\___/|_|     \n");
    printf("\n");
}

void
initial_condition()
{
    printf("\n");
    printf("                                  +--------------------------+\n");
    printf("                                  |                          |\n");
    printf("                                  |     +--------------+     |\n");
    printf("                                  |     |              |     |\n");
    printf("    +-----------------------------|-----|--------------|-----|-----------------------------+\n");
    printf("    |                             |     v              |     v                             |\n");
    printf("    |     +-----------+        +--+--------+        +--+--------+        +-----------+     |\n");
    printf("    |     | mod 4 = 0 |        |           |        |           |        |           |     |\n");
    printf("    +---->|-----------+------->|           +------->|           +------->|           +-----+\n");
    printf("          |           |        |    28     |        |     4     |        |    64     |\n");
    printf("    +-----+ hash head |<-------+           |<-------+           |<-------+           |<----+\n");
    printf("    |     |           |        |           |        |           |        |           |     |\n");
    printf("    |     +-----------+        +--------+--+        +--------+--+        +-----------+     |\n");
    printf("    |                             ^     |              ^     |                             |\n");
    printf("    +-----------------------------|-----|--------------|-----|-----------------------------+\n");
    printf("                                  |     |              |     |\n");
    printf("                                  |     +-----------------------------------------+\n");
    printf("                                  |                    |     |                    |\n");
    printf("                                  +-----------------------------------------+     |\n");
    printf("    +--------------------------------------------------|-----|--------------|-----|--------+\n");
    printf("    |                                                  |     v              |     v        |\n");
    printf("    |     +-----------+        +-----------+        +--+--------+        +--+--------+     |\n");
    printf("    |     | mod 4 = 1 |        |           |        |           |        |           |     |\n");
    printf("    +---->|-----------+------->|           +------->|           +------->|           +-----+\n");
    printf("          |           |        |    17     |        |     5     |        |    97     |\n");
    printf("    +-----+ hash head |<-------+           |<-------+           |<-------+           |<----+\n");
    printf("    |     |           |        |           |        |           |        |           |     |\n");
    printf("    |     +-----------+        +-----------+        +--------+--+        +--------+--+     |\n");
    printf("    |                                                  ^     |              ^     |        |\n");
    printf("    +--------------------------------------------------|-----|--------------|-----|--------+\n");
    printf("                                              +--------+     |              |     |\n");
    printf("                                              |              |              |     |\n");
    printf("                                              |  +-----------+              |     |\n");
    printf("                                              |  |                          |     |\n");
    printf("    +-----------------------------------------|--|--------------------------|-----|--------+\n");
    printf("    |                                         |  |                          |     v        |\n");
    printf("    |     +-----------+        +-----------+  |  |  +-----------+        +--+--------+     |\n");
    printf("    |     | mod 4 = 2 |        |           |  |  |  |           |        |           |     |\n");
    printf("    +---->|-----------+------->|           +--|--|->|           +------->|           +-----+\n");
    printf("          |           |        |    98     |  |  |  |    50     |        |    10     |\n");
    printf("    +-----+ hash head |<-------+           |<-|--|--+           |<-------+           |<----+\n");
    printf("    |     |           |        |           |  |  |  |           |        |           |     |\n");
    printf("    |     +-----------+        +-----------+  |  |  +-----------+        +--------+--+     |\n");
    printf("    |                                         |  |                          ^     |        |\n");
    printf("    +-----------------------------------------|--|--------------------------|-----|--------+\n");
    printf("                                              |  |                          |     |\n");
    printf("                                  +-----------+  |                 +--------+     |\n");
    printf("                                  |              |                 |              |\n");
    printf("                                  |     +--------+                 |  +-----------+\n");
    printf("    +-----------------------------|-----|--------------------------|--|--------------------+\n");
    printf("    |                             |     v                          |  |                    |\n");
    printf("    |     +-----------+        +--+--------+        +-----------+  |  |  +-----------+     |\n");
    printf("    |     | mod 4 = 3 |        |           |        |           |  |  |  |           |     |\n");
    printf("    +---->|-----------+------->|           +------->|           +--|--|->|           +-----+\n");
    printf("          |           |        |     3     |        |    35     |  |  |  |    99     |\n");
    printf("    +-----+ hash head |<-------+           |<-------+           |<-|--|--+           |<----+\n");
    printf("    |     |           |        |           |        |           |  |  |  |           |     +\n");
    printf("    |     +-----------+        +--------+--+        +-----------+  |  |  +-----------+     |\n");
    printf("    |                             ^     |                          |  |                    |\n");
    printf("    +-----------------------------|-----|--------------------------|--|--------------------+\n");
    printf("                                  |     |                          |  |\n");
    printf("                                  |     |                          |  |\n");
    printf("                                  |     |                          |  |\n");
    printf("          +-----------+           |     |                          |  |\n");
    printf(" +------->| free list +-----------+     |                          |  |\n");
    printf(" |  +-----+   head    |<----------------+                          |  |\n");
    printf(" |  |     +-----------+                                            |  |\n");
    printf(" |  |                                                              |  |\n");
    printf(" |  +--------------------------------------------------------------+  |\n");
    printf(" |                                                                    |\n");
    printf(" +--------------------------------------------------------------------+\n");
}

struct buf_header *
search_hash(int blkno)
{
    int h;
    struct buf_header *p;
    h = blkno%4;
    for (p = hash_head[h].hash_fp; p != &hash_head[h]; p = p->hash_fp) {
        if (p->blkno == blkno) {
            return p;
        }
    }
    return NULL;
}

int 
is_any_free_buffer()
{
    if (free_head.free_fp == &free_head) {
        return 0;
    }
    return 1;
}

void
insert_head(struct buf_header *h, struct buf_header *p)
{
    // insert p right after h.
    // [hash had] -> h ==> [hash head] -> h -> p
    p->hash_bp = h;
    p->hash_fp = h->hash_fp;
    h->hash_fp->hash_bp = p;
    h->hash_fp = p;
}

void
insert_tail(struct buf_header *h, struct buf_header *p)
{
    // insert p right brfore h.
    // [hash head] -> h ==> [hash head] -> p -> h
    p->hash_fp = h;
    p->hash_bp = h->hash_bp;
    h->hash_bp = p;
    p->hash_bp->hash_fp = p;
}

void
insert_head_free(struct buf_header *h, struct buf_header *p)
{
    // insert p right after h.
    // [free head] -> h ==> [free head] -> h -> p
    p->free_bp = h;
    p->free_fp = h->free_fp;
    h->free_fp->free_bp = p;
    h->free_fp = p;
}

void
insert_tail_free(struct buf_header *h, struct buf_header *p)
{
    // insert p right brfore h.
    // [free head] -> h ==> [free head] -> p -> h
    p->free_fp = h;
    p->free_bp = h->free_bp;
    h->free_bp = p;
    p->free_bp->free_fp = p;
}

void
remove_from_hash(struct buf_header *p)
{
    // remove p from hash list.
    // [hash head] -> bh1 -> bf2 -> p -> bf3 ==> [hash head] -> bf1 -> bf2 -> bf3
    p->hash_bp->hash_fp = p->hash_fp;
    p->hash_fp->hash_bp = p->hash_bp;
    p->hash_fp = NULL;
    p->hash_bp = NULL;
}

void
remove_from_free(struct buf_header *p)
{
    // remove p from free list.
    // [free head] -> bh1 -> bf2 -> p -> bf3 ==> [free head] -> bf1 -> bf2 -> bf3
    p->free_bp->free_fp = p->free_fp;
    p->free_fp->free_bp = p->free_bp;
    p->free_fp = NULL;
    p->free_bp = NULL;
}

char *
convert_status2letter(struct buf_header *p)
{
    strcpy(result, "------");
    if ((STAT_OLD & p->stat) == STAT_OLD) {
        result[0] = 'O';
    }
    if ((STAT_WAITED & p->stat) == STAT_WAITED) {
        result[1] = 'W';
    }
    if ((STAT_KRDWR & p->stat) == STAT_KRDWR) {
        result[2] = 'K';
    }
    if ((STAT_DWR & p->stat) == STAT_DWR) {
        result[3] = 'D';
    }
    if ((STAT_VALID & p->stat) == STAT_VALID) {
        result[4] = 'V';
    }
    if ((STAT_LOCKED & p->stat) == STAT_LOCKED) {
        result[5] = 'L';
    }
    return result;
}

unsigned int
convert_letter2status(char p)
{
    if (p == 'L' || p == 'l') {
        return STAT_LOCKED;
    } else if (p == 'V' || p == 'v') {
        return STAT_VALID;
    } else if (p == 'D' || p == 'd') {
        return STAT_DWR;
    } else if (p == 'K' || p == 'k') {
        return STAT_KRDWR;
    } else if (p == 'W' || p == 'w') {
        return STAT_WAITED;
    } else if (p == 'O' || p == 'o') {
        return STAT_OLD;
    } 
    return 0;
}

void
make_free_list_empty()
{
    // all reset
    buf0.stat |= STAT_LOCKED;
    buf1.stat |= STAT_LOCKED;
    buf2.stat |= STAT_LOCKED;
    buf3.stat |= STAT_LOCKED;
    buf4.stat |= STAT_LOCKED;
    buf5.stat |= STAT_LOCKED;
    buf6.stat |= STAT_LOCKED;
    buf7.stat |= STAT_LOCKED;
    buf8.stat |= STAT_LOCKED;
    buf9.stat |= STAT_LOCKED;
    buf10.stat |= STAT_LOCKED;
    buf11.stat |= STAT_LOCKED;

    buf0.stat |= STAT_VALID;
    buf1.stat |= STAT_VALID;
    buf2.stat |= STAT_VALID;
    buf3.stat |= STAT_VALID;
    buf4.stat |= STAT_VALID;
    buf5.stat |= STAT_VALID;
    buf6.stat |= STAT_VALID;
    buf7.stat |= STAT_VALID;
    buf8.stat |= STAT_VALID;
    buf9.stat |= STAT_VALID;
    buf10.stat |= STAT_VALID;
    buf11.stat |= STAT_VALID;

    buf0.stat &= ~STAT_DWR;
    buf1.stat &= ~STAT_DWR;
    buf2.stat &= ~STAT_DWR;
    buf3.stat &= ~STAT_DWR;
    buf4.stat &= ~STAT_DWR;
    buf5.stat &= ~STAT_DWR;
    buf6.stat &= ~STAT_DWR;
    buf7.stat &= ~STAT_DWR;
    buf8.stat &= ~STAT_DWR;
    buf9.stat &= ~STAT_DWR;
    buf10.stat &= ~STAT_DWR;
    buf11.stat &= ~STAT_DWR;

    buf0.stat &= ~STAT_KRDWR;
    buf1.stat &= ~STAT_KRDWR;
    buf2.stat &= ~STAT_KRDWR;
    buf3.stat &= ~STAT_KRDWR;
    buf4.stat &= ~STAT_KRDWR;
    buf5.stat &= ~STAT_KRDWR;
    buf6.stat &= ~STAT_KRDWR;
    buf7.stat &= ~STAT_KRDWR;
    buf8.stat &= ~STAT_KRDWR;
    buf9.stat &= ~STAT_KRDWR;
    buf10.stat &= ~STAT_KRDWR;
    buf11.stat &= ~STAT_KRDWR;

    buf0.stat &= ~STAT_WAITED;
    buf1.stat &= ~STAT_WAITED;
    buf2.stat &= ~STAT_WAITED;
    buf3.stat &= ~STAT_WAITED;
    buf4.stat &= ~STAT_WAITED;
    buf5.stat &= ~STAT_WAITED;
    buf6.stat &= ~STAT_WAITED;
    buf7.stat &= ~STAT_WAITED;
    buf8.stat &= ~STAT_WAITED;
    buf9.stat &= ~STAT_WAITED;
    buf10.stat &= ~STAT_WAITED;
    buf11.stat &= ~STAT_WAITED;

    buf0.stat &= ~STAT_OLD;
    buf1.stat &= ~STAT_OLD;
    buf2.stat &= ~STAT_OLD;
    buf3.stat &= ~STAT_OLD;
    buf4.stat &= ~STAT_OLD;
    buf5.stat &= ~STAT_OLD;
    buf6.stat &= ~STAT_OLD;
    buf7.stat &= ~STAT_OLD;
    buf8.stat &= ~STAT_OLD;
    buf9.stat &= ~STAT_OLD;
    buf10.stat &= ~STAT_OLD;
    buf11.stat &= ~STAT_OLD;

    buf0.free_fp = NULL;
    buf1.free_fp = NULL;
    buf2.free_fp = NULL;
    buf3.free_fp = NULL;
    buf4.free_fp = NULL;
    buf5.free_fp = NULL;
    buf6.free_fp = NULL;
    buf7.free_fp = NULL;
    buf8.free_fp = NULL;
    buf9.free_fp = NULL;
    buf10.free_fp = NULL;
    buf11.free_fp = NULL;
    buf0.free_bp = NULL;
    buf1.free_bp = NULL;
    buf2.free_bp = NULL;
    buf3.free_bp = NULL;
    buf4.free_bp = NULL;
    buf5.free_bp = NULL;
    buf6.free_bp = NULL;
    buf7.free_bp = NULL;
    buf8.free_bp = NULL;
    buf9.free_bp = NULL;
    buf10.free_bp = NULL;
    buf11.free_bp = NULL;

    free_head.free_fp = &free_head;
    free_head.free_bp = &free_head;

    fprintf(stderr, "Make free list empty and all buffer's state is valid and locked.\n");
}