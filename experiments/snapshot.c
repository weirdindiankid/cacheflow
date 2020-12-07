/*************************************************************/
/*                                                           */
/*  Unified code to launch benchmarks with periodic          */
/*  snapshotting. This code was initially written by:        */
/*  Steven Brzozowski (BU)                                   */
/*  Major rev. by: Dharmesh Tarapore (BU)                    */
/*  Major rev. by: Renato Mancuso (BU)                       */
/*                                                           */
/*  Date: April 2020                                         */
/*                                                           */
/*************************************************************/

#define _GNU_SOURCE
#include "params.h"
#include <limits.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sched.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>

#define MAX_BENCHMARKS 20
#define PARENT_CPU 2
#define SNAP_PERIOD_MS 5

#define USAGE_STR "Usage: %s [-rmafi] [-o outpath] [-p period_ms] "	\
	"\"benchmark 1\", ..., \"benchmark n\"\n"			\
	"Options:\n"							\
	"-r\tSet real-time priorities. Parent has highest priority,\n"	\
	"  \tthe priority of the benchmarks is set in decreasing order.\n" \
	"\n"								\
	"-m\tMimic only. No cache snapshotting but do everything else.\n" \
	"\n"								\
	"-a\tAsynchrnonous mode. Do not send SIGTOP/SIGCONT to benchmarks.\n" \
	"\n"								\
	"-f\tForce output. Overwrite content of output directory.\n"	\
	"\n"								\
	"-i\tIsolation mode. Pin parent alone on CPU "STR(PARENT_CPU)".\n" \
	"\n" \
	"-o\tOutput files to custom directory instead of " SCRATCHSPACE_DIR ".\n" \
	"\n" \
	"-p\tSet custom period between samples expressed in msec. Default is " STR(SNAP_PERIOD_MS) " msec.\n\n" \
	"-n\tDo not perform physical->virtual address translation in the kernel.\n\n" \
	"-t\tOperate in transparent mode, i.e. defer acquisition of samples to disk to the end.\n\n" \
	"-l\tDo not acquire the memory layout of the observed benchmarks.\n\n" \
        "-h\tOperate in overhead measurement mode. Only 2 back-to-back snapshots will be acquired.\n" \
	"\n"

#define MS_TO_NS(ms) \
	(ms * 1000 * 1000)
#define MALLOC_CMD_PAD (32)


int flag_rt = 0;
int flag_out = 0;
int flag_force = 0;
int flag_isol = 0;
int flag_async = 0;
int flag_mimic = 0;
int flag_resolve = 1;
int flag_transparent = 0;
int flag_bm_layout = 1;
int flag_overhead = 0;
int flag_periodic = 1;

/* Default sampling period is 5 ms */
long int snap_period_ms = SNAP_PERIOD_MS;

char * outdir = SCRATCHSPACE_DIR;

int bm_count = 0;
int running_bms;
char * bms [MAX_BENCHMARKS];
pid_t pids [MAX_BENCHMARKS];

int max_prio;

volatile int done = 0;
int snapshots = 0;

/* Use user-specified parameters to configure the kernel module for
 * acquisition */
int config_shutter(void);

/* Function to spawn all the listed benchmarks */
void launch_benchmarks(void);

/* Handler for SIGCHLD signal to detect benchmark termination */
void proc_exit_handler (int signo, siginfo_t * info, void * extra);

/* Ask the kernel to acquire a new snapshot */
void acquire_new_snapshot(void);

/* Handler for SIGRTMAX signal to initiate new snapshot */
void snapshot_handler (int signo, siginfo_t * info, void * extra);

/* Set real-time SCHED_FIFO scheduler with given priority */
void set_realtime(int prio);

/* Only change RT prio of calling process */
void change_rt_prio(int prio);

/* Set non-real-time SCHED_OTHER scheduler */
void set_non_realtime(void);

/* Install periodic snapshot handler and wait for completion using signals */
void wait_completion(void);

/* Entry function to interface with the kernel module via the proc interface */
void read_cache_to_file(char * filename, int index);

/* Function to complete execution */
void wrap_up(void);

int main (int argc, char ** argv)
{
	/* Parse command line */
	int opt, res;
	struct stat dir_stat;
	
	while ((opt = getopt(argc, argv, "-rmafio:p:ntlh")) != -1) {
		switch (opt) {
		case 1:
		{
			/* Benchmark to run parameter */
			bms[bm_count++] = argv[optind - 1];
			break;
		}
		case 'r':
		{
			/* RT scheduler requested */			
			flag_rt = 1;
			break;
		}
		case 'a':
		{
			/* Asynchronous requested */
			flag_async = 1;
			break;
		}
		case 'm':
		{
			/* Mimic only --- no cache dumps performed */
			flag_mimic = 1;
			break;
		}
		case 'i':
		{
			/* Isolate: make sure child processes are not
			 * allowed to run on the same CPU as the
			 * parent */
			flag_isol = 1;
			break;
		}
		case 'f':
		{
			/* Override content of outdir !!! CAREFUL */			
			flag_force = 1;
			break;
		}
		case 'p':
		{
			/* Set custom sampling period in ms */			
			snap_period_ms = strtol(optarg, NULL, 10);

			if (snap_period_ms == 0)
			    flag_periodic = 0;
			
			break;
		}
		case 'n':
		{
			/* Disable address resolution in the kernel  */			
			flag_resolve = 0;
			break;
		}
		case 't':
		{
			/* Request transparent snapshotting */			
			flag_transparent = 1;
			break;
		}
		case 'l':
		{
			/* Do not acquire applications layout files
			 * with snapshots */			
			flag_bm_layout = 0;
			break;
		}
		case 'h':
		{
			/* Operate in overhead calculation mode. In
			 * this mode, the scnapshot will be activated
			 * in one-shot mode after the amount of time
			 * specified with the -p parameter. Two
			 * back-to-back snapshots will be collected. */			
			flag_overhead = 1;
			break;
		}
		case 'o':
		{
			/* Custom output dir requested */
			int path_len = strlen(optarg);
			outdir = (char *)malloc(path_len+1);
			strncpy(outdir, optarg, path_len);
			flag_out = 1;
			break;
		}
		default:
		{
			/* '?' */
			fprintf(stderr, USAGE_STR, argv[0]);
			exit(EXIT_FAILURE);
		}
		}
	} 

	if (bm_count == 0) {
		fprintf(stderr, USAGE_STR, argv[0]);
		exit(EXIT_FAILURE);		
	}

	/* Prepare output directory */
	res = stat(outdir, &dir_stat);
	if (!flag_force && res >= 0) {
		fprintf(stderr, "Output directory already exists. Use -f to override files.\n");
		exit(EXIT_FAILURE);
	} else if (flag_force && res >= 0){
		char * __cmd = (char *)malloc(strlen(outdir) + MALLOC_CMD_PAD);
		sprintf(__cmd, "rm -rf %s", outdir);
		printf("Erasing content of %s\n", outdir);
		/* Do not actually do this for now. Just to be safe. */
		printf("[%s]\n", __cmd);
		free(__cmd);
	} else {
		/* The directory does not exist. */
		mkdir(outdir, 0700);
	}
	
	/* ALWAYS run the parent with top RT priority */
	max_prio = sched_get_priority_max(SCHED_FIFO);
	set_realtime(max_prio);

	/* Send setup commands to the kernel module */
	config_shutter();
	
	/* Done with command line parsing -- time to fire up the benchmarks */
	launch_benchmarks();

	/* Done with benchmarks --- wait for completion using an asynch handler */
	wait_completion();

	/* Almost done - wrap up by creating pid.txt file */
	wrap_up();
	
	/* Deallocate any malloc'd memory before exiting */
	if (flag_out) {
		free(outdir);
	}
		
	return EXIT_SUCCESS;
}

static inline int open_mod(void)
{
	int fd;
	
	/* Open dumpcache interface */
	if (((fd = open(PROC_FILENAME, O_RDONLY)) < 0)) {
		perror("Failed to open "PROC_FILENAME" file. Is the module inserted?");
		exit(EXIT_FAILURE);
	}

	return fd;
}

/* Use user-specified parameters to configure the kernel module for
 * acquisition */
int config_shutter(void)
{
	int dumpcache_fd;
	int err;
	unsigned long cmd = 0;

	dumpcache_fd = open_mod();
	
	/* Whatever is the mode, reset the sample pointer to start
	 * with. */
	cmd |= DUMPCACHE_CMD_SETBUF_SHIFT;

	/* If we want transparent mode, request buffer auto-increment
	 * in the kernel */
	if (flag_transparent == 1) {
		cmd |= DUMPCACHE_CMD_AUTOINC_EN_SHIFT;
	} else {
		cmd |= DUMPCACHE_CMD_AUTOINC_DIS_SHIFT;
	}

	/* Disable address resolution if requested by the user */
	if (flag_resolve == 1) {
		cmd |= DUMPCACHE_CMD_RESOLVE_EN_SHIFT;
	} else {
		cmd |= DUMPCACHE_CMD_RESOLVE_DIS_SHIFT;
	}
	
	err = ioctl(dumpcache_fd, DUMPCACHE_CMD_CONFIG, cmd);
	if (err) {
		perror("Shutter configuration command failed");
		exit(EXIT_FAILURE);
	}

	printf("Module config OKAY!\n");
	
	close(dumpcache_fd);

	return err;
}

/* Function to spawn all the listed benchmarks */
void launch_benchmarks (void)
{
	int i;

	for (i = 0; i < bm_count; ++i) {

		/* Launch all the BMs one by one */		
		pid_t cpid = fork();
		if (cpid == -1) {
			perror("fork");
			exit(EXIT_FAILURE);
		}
		/* Child process */
		if (cpid == 0) {
			/* Assume that there is only at most one paramer */
			char * args[3];

			/* To follow execv's convention*/
			args[2] = NULL;
			args[0] = bms[i];

			if((args[1] = strchr(bms[i], ' '))) {
				*args[1] = '\0';
				args[1]++;			
			}

			/* Set SCHED_FIFO priority if necessary */
			if (flag_rt) {
				change_rt_prio(max_prio -1 -i);
			} else {
				set_non_realtime();
			}

			sched_yield();
			
			execv(args[0], args);			
			
			/* This point can only be reached if execl fails. */
			perror("Unable to run benchmark");
			exit(EXIT_FAILURE);
		}
		/* Parent process */	       
		else {
			/* Keep track of the new bm that has been launched */
			printf("Running: %s (PID = %d, prio = %d)\n", bms[i], cpid,
			       (flag_rt?(max_prio -1 -i):0));
			
			pids[running_bms++] = cpid;
			//cpid_arr[i*NUM_SD_VBS_BENCHMARKS_DATASETS+j] = cpid;
		}
		

	}
	
}

/* Handler for SIGCHLD signal to detect benchmark termination */
/* Adapted from https://docs.oracle.com/cd/E19455-01/806-4750/signals-7/index.html */
void proc_exit_handler (int signo, siginfo_t * info, void * extra)
{
	int wstat;
	pid_t pid;
	
	(void)signo;
	(void)info;
	(void)extra;
	
	for (;;) {
		pid = waitpid (-1, &wstat, WNOHANG);
		if (pid == 0)
			/* No change in the state of the child(ren) */
			return;
		else if (pid == -1) {
			/* Something went wrong */
			perror("Waitpid() exited with error");
			exit(EXIT_FAILURE);
			return;
		}
		else {
			printf ("PID %d Done. Return code: %d\n", pid, WEXITSTATUS(wstat));

			/* Detect completion of all the benchmarks */
			if(--running_bms == 0) {
				done = 1;
				return;
			}
		}
	}
}

/* Copy content of file from src to dst */
#define BUF_SIZE (1024)
void copy_file(char * src, char * dst)
{
	static char buf [BUF_SIZE];
	int src_fd = open(src, O_RDONLY);
	ssize_t num_read;
	
	if (src_fd < 0)
		return;

	int dst_fd = open(dst, O_RDWR | O_CREAT | O_TRUNC, 0700);

	if (dst_fd < 0) {
		perror("Unable to save maps file.");
		exit(EXIT_FAILURE);
	}

	while ((num_read = read(src_fd, buf, BUF_SIZE)) > 0)
	{
		if (write(dst_fd, buf, num_read) != num_read) {
			perror("Unable to write maps file.");
			exit(EXIT_FAILURE);
		}
	}

	close(src_fd);
	close(dst_fd);	
}

/* Ask the kernel to acquire a new snapshot */
void acquire_new_snapshot(void)
{
	int dumpcache_fd = open_mod();
	int retval;

	retval = ioctl(dumpcache_fd, DUMPCACHE_CMD_SNAPSHOT, 0);
	if (retval < 0) {
		fprintf(stderr, "Return was %d\n", retval);
		perror("Unable to commandeer new snapshot acquisition");
		exit(EXIT_FAILURE);
	}
	
	close(dumpcache_fd);
}

/* Handler for SIGRTMAX signal to initiate new snapshot */
void snapshot_handler (int signo, siginfo_t * info, void * extra)
{
	(void)signo;
	(void)extra;

	static char * __cmd = NULL;
	static char __proc_entry[MALLOC_CMD_PAD];
	
	static struct itimerspec it = {
		.it_value.tv_sec = 0,
		.it_value.tv_nsec = 0,
		.it_interval.tv_sec = 0,
		.it_interval.tv_nsec = 0, /* One shot mode */
	};
	
	timer_t timer = *((timer_t *)(info->si_value.sival_ptr));
	int i;


	/* Set next activation */
	it.it_value.tv_nsec = MS_TO_NS(snap_period_ms);
		
	/* Should happen only once */
	if (!__cmd)
		__cmd = (char *)malloc(strlen(outdir) + MALLOC_CMD_PAD);

	/* Send SIGSTOP to all the children (skip in async mode) */
	for (i = 0; i < bm_count && !flag_async; ++i) {
		kill(pids[i], SIGSTOP);
	}
	
	/* Skip all of this in mimic mode */
	if(!flag_mimic) {
		/* Ask the module to acquire a new snapshot.*/
		acquire_new_snapshot();

		/* Unless we are in transparent mode, save cache dump
		 * to file right away */
		if (!flag_transparent) {
			sprintf(__cmd, "%s/cachedump%d.csv", outdir, snapshots);

			/* In non-transparent mode, no autoincrement
			 * is selected in the kernel, so we always
			 * read the first buffer. */
			read_cache_to_file(__cmd, 0);
		}
	}

	/* Acquire maps files if layout acquisition is selected */
	if (flag_bm_layout) {
		/* Initiate a /proc/pid/maps dump to file */
		for (i = 0; i < bm_count; ++i) {
			sprintf(__cmd, "%s/%d-%d.txt", outdir, pids[i], snapshots);
			sprintf(__proc_entry, "/proc/%d/maps", pids[i]);
			copy_file(__proc_entry, __cmd);
		}
	}

	/* Resume all the children with SIGCONT (skip in async mode) */
	for (i = 0; i < bm_count && !flag_async; ++i) {
		kill(pids[i], SIGCONT);
	}
		
	/* Keep track of the total number of snapshots acquired so far */
	++snapshots;
	
	/* If this is snapshot #1, then just acquire another snapshot
	 * right away and that will be it. */
	if (flag_overhead) {
		if (snapshots == 1) {
			/* Set timer's next activation */
			timer_settime(timer, 0, &it, NULL);			
		}
		if (snapshots == 2) {
			snapshot_handler (signo, info, extra);
		}
	} else {
		/* Set timer's next activation */
		timer_settime(timer, 0, &it, NULL);		
	}
	
}

/* Handler for SIGRTMAX-1 signal to initiate new snapshot */
void ext_snapshot_handler (int signo, siginfo_t * info, void * extra)
{
	(void)signo;
	(void)extra;

	static char * __cmd = NULL;
	static char __proc_entry[MALLOC_CMD_PAD];
	
	int i;
	
	/* Should happen only once */
	if (!__cmd)
		__cmd = (char *)malloc(strlen(outdir) + MALLOC_CMD_PAD);

	/* Send SIGSTOP to all the children (skip in async mode) */
	for (i = 0; i < bm_count && !flag_async; ++i) {
		kill(pids[i], SIGSTOP);
	}
	
	/* Skip all of this in mimic mode */
	if(!flag_mimic) {
		/* Ask the module to acquire a new snapshot.*/
		acquire_new_snapshot();

		/* Unless we are in transparent mode, save cache dump
		 * to file right away */
		if (!flag_transparent) {
			sprintf(__cmd, "%s/cachedump%d.csv", outdir, snapshots);

			/* In non-transparent mode, no autoincrement
			 * is selected in the kernel, so we always
			 * read the first buffer. */
			read_cache_to_file(__cmd, 0);
		}
	}

	/* Acquire maps files if layout acquisition is selected */
	if (flag_bm_layout) {
		/* Initiate a /proc/pid/maps dump to file */
		for (i = 0; i < bm_count; ++i) {
			sprintf(__cmd, "%s/%d-%d.txt", outdir, pids[i], snapshots);
			sprintf(__proc_entry, "/proc/%d/maps", pids[i]);
			copy_file(__proc_entry, __cmd);
		}
	}

	/* Resume all the children with SIGCONT (skip in async mode) */
	for (i = 0; i < bm_count && !flag_async; ++i) {
		kill(pids[i], SIGCONT);
	}
		
	/* Keep track of the total number of snapshots acquired so far */
	++snapshots;
	
}


/* Function to complete execution */
void wrap_up (void)
{
	char * pathname;
	int pids_fd, len, i;
	
	pathname = (char *)malloc(strlen(outdir) + MALLOC_CMD_PAD);

	/* If we are running in transparent mode, now it's the time to
	 * dump all the snapshots. */
	if (flag_transparent) {
		int dumpcache_fd = open_mod();
		int retval;

		retval = ioctl(dumpcache_fd, DUMPCACHE_CMD_CONFIG, DUMPCACHE_CMD_GETBUF_SHIFT);
		if (retval < 0) {
			perror("Unable to retrieve current buffer index from shutter");
			exit(EXIT_FAILURE);
		}

		/* The read_cache_to_file function will re-open this
		 * file. So close it for now. */
		close(dumpcache_fd);
		
		/* If the number of snapshots is in mismatch,
		 * something is wrong! */
		if (retval != snapshots) 
			fprintf(stderr, "WARNING: Number of snapshots does not match the expected"
				"value. Possible overflow?\n");
		
		for (i = 0; i < retval; ++i) {
			sprintf(pathname, "%s/cachedump%d.csv", outdir, i);
			read_cache_to_file(pathname, i);
		}		

	}	
	
	/* Now create pids file with metadata about the acquisition */
	sprintf(pathname, "%s/pids.txt", outdir);
	pids_fd = open(pathname, O_CREAT | O_TRUNC | O_RDWR, 0700);

	if (pids_fd < 0) {
		perror("Unable to write pids file");
		exit(EXIT_FAILURE);
	}

	/* Write out number of snapshots */
	len = sprintf(pathname, "%d\n", snapshots);
	write(pids_fd, pathname, len);

	/* Write out PID of parent process */
	len = sprintf(pathname, "%d\n", getpid());
	write(pids_fd, pathname, len);
	
	for (i = 0; i < bm_count; ++i) {
		len = sprintf(pathname, "%d\n", pids[i]);
		write(pids_fd, pathname, len);
	}

	close(pids_fd);
	free(pathname);
}

/* Set real-time SCHED_FIFO scheduler with given priority */
void set_realtime(int prio)
{
	struct sched_param sp;
	
	/* Initialize parameters */
	memset(&sp, 0, sizeof(struct sched_param));
	sp.sched_priority = prio;
	
	/* Attempt to set the scheduler for current process */
	if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0) {
		perror("Unable to set SCHED_FIFO scheduler");
		exit(EXIT_FAILURE);
	}

	/* Set CPU affinity if isolate flag specified */
	if (flag_isol) {
		cpu_set_t set;
		CPU_ZERO(&set);

		/* default to CPU x for parent */
		CPU_SET(PARENT_CPU, &set);
		
		if (sched_setaffinity(getpid(), sizeof(set), &set) == -1) {
			perror("Unable to set CPU affinity.");
			exit(EXIT_FAILURE);			
		}

	}
}

/* Only change RT prio of calling process */
void change_rt_prio(int prio)
{
	struct sched_param sp;
	
	/* Initialize attributes */
	memset(&sp, 0, sizeof(struct sched_param));
	sp.sched_priority = prio;
	
	if(sched_setparam(0, &sp) < 0) {
		perror("Unable to set new RT priority");
		exit(EXIT_FAILURE);		
	}

	/* Set CPU affinity if isolate flag specified */
	if (flag_isol) {
		cpu_set_t set;
		int i, nprocs;

		nprocs = get_nprocs();
		
		CPU_ZERO(&set);

		/* default to CPU x for parent */
		for (i = 0; i < nprocs; ++i) {
			if (i != PARENT_CPU)
				CPU_SET(i, &set);
		}
		
		if (sched_setaffinity(getpid(), sizeof(set), &set) == -1) {
			perror("Unable to set CPU affinity.");
			exit(EXIT_FAILURE);			
		}

	}
	
}

/* Set non-real-time SCHED_OTHER scheduler */
void set_non_realtime(void)
{
	struct sched_param sp;
	
	/* Initialize parameters */
	memset(&sp, 0, sizeof(struct sched_param));
	
	/* Attempt to set the scheduler for current process */
	if (sched_setscheduler(0, SCHED_OTHER, &sp) < 0) {
		perror("Unable to set SCHED_OTHER scheduler");
		exit(EXIT_FAILURE);
	}	
}

/* Install periodic snapshot handler and wait for completion using signals */
void wait_completion(void)
{
	sigset_t waitmask;
	struct sigaction chld_sa;
	struct sigaction timer_sa;
	struct sigaction ext_sa;
	
	struct sigevent ev;
	struct itimerspec it;
	timer_t timer;
	
	/* Use RT POSIX extension */
	chld_sa.sa_flags = SA_SIGINFO;
	chld_sa.sa_sigaction = proc_exit_handler;
	sigemptyset(&chld_sa.sa_mask);
	sigaddset(&chld_sa.sa_mask, SIGCHLD);

	/* Install SIGCHLD signal handler */
	sigaction(SIGCHLD, &chld_sa, NULL);

	/* Setup timer */
	timer_sa.sa_flags = SA_SIGINFO;
	timer_sa.sa_sigaction = snapshot_handler;
	sigemptyset(&timer_sa.sa_mask);
	sigaddset(&timer_sa.sa_mask, SIGRTMAX);
	sigaddset(&timer_sa.sa_mask, SIGRTMAX-1);

	/* Install SIGRTMAX signal handler */
	sigaction(SIGRTMAX, &timer_sa, NULL);

	/* Setup signal hanlder for external trigger */
	ext_sa.sa_flags = SA_SIGINFO;
	ext_sa.sa_sigaction = ext_snapshot_handler;
	sigemptyset(&ext_sa.sa_mask);
	sigaddset(&ext_sa.sa_mask, SIGRTMAX);
	sigaddset(&ext_sa.sa_mask, SIGRTMAX-1);

	/* Install SIGRTMAX signal handler */
	sigaction(SIGRTMAX-1, &ext_sa, NULL);
		
	/* Timer creation */
	memset(&ev, 0, sizeof(ev));
	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = SIGRTMAX;

	/* Pass a reference to the timer. */
	ev.sigev_value.sival_ptr = (void *)&timer;

	timer_create(CLOCK_REALTIME, &ev, &timer);

	memset(&it, 0, sizeof(it));
	it.it_value.tv_sec = 0;
	it.it_value.tv_nsec = 1; /* Start immediately */
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_nsec = 0; /* One shot mode for now. */


	/* Start timer only if we are operating in periodic mode */
	if (flag_periodic) {
		timer_settime(timer, 0, &it, NULL);
	}
	    
	printf("Setup completed!\n");
	
	/* Wait for any signal */
	sigemptyset(&waitmask);
	while(!done){
		sigsuspend(&waitmask);
	}

	timer_delete(timer);
}


/* Entry function to interface with the kernel module via the proc interface */
void read_cache_to_file(char * filename, int index) {
	int outfile;
	int dumpcache_fd;
	int bytes_to_write = 0;
	struct cache_sample * cache_contents;
	
	char csv_file_buf[WRITE_SIZE + 10*CSV_LINE_SIZE];
	int cache_set_idx, cache_line_idx;
	
	cache_contents = NULL;
	
	if (((outfile = open(filename, O_CREAT | O_WRONLY | O_SYNC | O_TRUNC, 0666)) < 0)) {
		perror("Failed to open outfile");
		exit(EXIT_FAILURE);
	}
	
	if (!cache_contents) {
	    cache_contents = (struct cache_sample *)malloc(sizeof(struct cache_sample));
	}

	dumpcache_fd = open_mod();

	if (flag_transparent) {
		/* Make sure to ask the kernel for the buffer at the
		 * specific index. */
		int retval;
		unsigned long cmd = DUMPCACHE_CMD_SETBUF_SHIFT;

		cmd |= DUMPCACHE_CMD_VALUE(index);
		
		retval = ioctl(dumpcache_fd, DUMPCACHE_CMD_CONFIG, cmd);
		if (retval < 0) {
			perror("Unable to retrieve current buffer index from shutter");
			exit(EXIT_FAILURE);
		}

	}
	
	if (read(dumpcache_fd, cache_contents, sizeof(struct cache_sample)) < 0) {
		perror("Failed to read from proc file");
		exit(EXIT_FAILURE);
	}
	
	for (cache_set_idx = 0; cache_set_idx < NUM_CACHESETS; cache_set_idx++) {
		for (cache_line_idx = 0; cache_line_idx < NUM_CACHELINES; cache_line_idx++) {
			bytes_to_write += sprintf(csv_file_buf + bytes_to_write,
						  "%05d,0x%08llx\n",
						  cache_contents->sets[cache_set_idx]
						  .cachelines[cache_line_idx].pid,
						  cache_contents->sets[cache_set_idx]
						  .cachelines[cache_line_idx].addr);
			
			/* Flush out pending data */			
			if (bytes_to_write >= WRITE_SIZE){				
				if (write(outfile, csv_file_buf, bytes_to_write) == -1) {
					perror("Failed to write to outfile");
				}
				bytes_to_write = 0;
			}
		}   
	}
	
	/* Flush out leftover buffer data */
	if (bytes_to_write){
		if (write(outfile, csv_file_buf, bytes_to_write) == -1) {
			perror("Failed to write to outfile");
		}
	}
	
	close(outfile);
	close(dumpcache_fd);
}
