#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

#define __STR(x) #x
#define STR(x) __STR(x)

// Each cache set/way coordinate has a 1-5 digit pid followed by a
// comma then 12 digit tag prepended with '0x' and ended with a newline
#define CSV_LINE_SIZE 5 + 1 + (2 + 12) + 1
#define WRITE_SIZE 35 * 1024 // 32 kb (8 4kb pages)
#define NUM_CACHESETS 2048
#define CACHESIZE 1024*1024*2
#define NUM_CACHELINES 16
#define PROC_FILENAME "/proc/dumpcache"

#define SCRATCHSPACE_DIR "/tmp/dumpcache"
#define PIPENV_DIR "/home/nvidia/.local/bin/pipenv"
#define MICROSECONDS_IN_MILLISECONDS 1000
#define MILLISECONDS_BETWEEN_SAMPLES 10 * MICROSECONDS_IN_MILLISECONDS

/* SD-VBS Params */
//#define NUM_SD_VBS_BENCHMARKS 1
/* Set to 7 to run all */
//#define NUM_SD_VBS_BENCHMARKS_DATASETS 2


// Struct representing a single cache line - each cacheline struct is 68 bytes
struct cache_line
{
	pid_t pid;
	uint64_t addr;
};

struct cache_set {
	struct cache_line cachelines[NUM_CACHELINES];
};

struct cache_sample {
	struct cache_set sets[NUM_CACHESETS];
};

#define NUM_ITERATIONS 3
#define BASE_BUFFSIZE_MB 2.0


/* Defines for commands to the kernel module */
/* Command to access the configuration interface */
#define DUMPCACHE_CMD_CONFIG _IOW(0, 0, unsigned long)
/* Command to initiate a cache dump */
#define DUMPCACHE_CMD_SNAPSHOT _IOW(0, 1, unsigned long)

#define DUMPCACHE_CMD_VALUE_WIDTH  16 
#define DUMPCACHE_CMD_VALUE_MASK   ((1 << DUMPCACHE_CMD_VALUE_WIDTH) - 1)
#define DUMPCACHE_CMD_VALUE(cmd)		\
	(cmd & DUMPCACHE_CMD_VALUE_MASK)

/* Command to set the current buffer number */
#define DUMPCACHE_CMD_SETBUF_SHIFT           (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 1))

/* Command to retrievet the current buffer number */
#define DUMPCACHE_CMD_GETBUF_SHIFT           (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 2))

/* Command to enable/disable buffer autoincrement */
#define DUMPCACHE_CMD_AUTOINC_EN_SHIFT       (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 3))
#define DUMPCACHE_CMD_AUTOINC_DIS_SHIFT      (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 4))

/* Command to enable/disable address resolution */
#define DUMPCACHE_CMD_RESOLVE_EN_SHIFT       (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 5))
#define DUMPCACHE_CMD_RESOLVE_DIS_SHIFT      (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 6))

/* Command to enable/disable snapshot timestamping */
#define DUMPCACHE_CMD_TIMESTAMP_EN_SHIFT       (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 7))
#define DUMPCACHE_CMD_TIMESTAMP_DIS_SHIFT      (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 8))
