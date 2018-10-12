#define NHASH 4

#define STAT_LOCKED  0x00000001
#define STAT_VALID   0x00000002
#define STAT_DWR     0x00000004
#define STAT_KRDWR   0x00000008
#define STAT_WAITED  0x00000010
#define STAT_OLD     0x00000020

struct buf_header {
    int blkno;
    struct buf_header* hash_fp;
    struct buf_header* hash_bp;
    struct buf_header *free_fp;
    struct buf_header *free_bp;
    unsigned int stat;
    char *cache_data;
};
