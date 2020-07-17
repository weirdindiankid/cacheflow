/*
 * Goal: run SD-VBS using automated script
 * chdir into sd-vbs-bin dir
 * For each dir in sd-vbs-bin:
 *	- chdir into data dir
 *  - for each dir in the data dir:
 *    - for each iteration in NUM_RUNS:
 *          -  Port to C and execute res=$(./$b . | grep Cycles | awk '{print $4}')
 *          -  echo "$b; $d; $i; $res"
 * 	- Get PID
 *  - Get proc map
 *  - Dump PID and proc map to /tmp/dumpcache
 */

/*
 * Single core: {disparity: [cif]}
 * Single core 2: {disparity: [vga]}
 *
 * Multiple benchmarks single core
 * Single core: {disparity: [vga], mser: [vga], sift: [cif], tracking: [cif]} (default scheduler / SCHED_OTHER)
 * Single core: {disparity: [vga], mser: [vga], sift: [cif], tracking: [cif]} (SCHED_FIFO)
 *
 * ----------------------------- Multi core ---------------------------------------------------
 * Multi core: {disparity: [vga], mser: [vga]}
 * Multi core: {disparity: [vga], mser: [vga], sift: [cif], tracking: [cif]} (SCHED_FIFO)
 * 
 * [Time by commenting out dumpcache and dump proc maps and time with both active to estimate overhead]
 */

#include "params.h"
#include <limits.h>
#include <unistd.h>

/*
 * TODO: stat directories eventually to avoid hardcoding which benchmarks to run.
 * I am leaving these as a list since it's easier to comment out benchmarks I 
 * don't want to see than move folders around. 
 */
static const char *BASE_SD_VBS_PATH = "./sd-vbs-bin/";
static const char *list_of_benchmarks[] = {"disparity", "mser", "sift", "tracking"};
//{"disparity", "localization", "mser", "sift","texture_synthesis", "tracking"};

static const short NUM_SD_VBS_BENCHMARKS = 4;
/* Set to 7 to run all */
static const short NUM_SD_VBS_BENCHMARKS_DATASETS = 1;

/* In increasing order of CPU cycle consumption: 
 * http://cseweb.ucsd.edu/~mbtaylor/vision/release/SD-VBS.pdf
 */
static const char *list_of_datasets[] = {"vga", "cif"};//{"cif", "qcif", "sqcif", "sim", "sim_fast", "fullhd", "vga"};

/* Originally 30 in the bash script. */
static const short NUM_SD_VBS_BENCHMARKS_ITERS = 1;

static short NUM_TOTAL_EXECUTABLES = 4; //NUM_SD_VBS_BENCHMARKS * NUM_SD_VBS_BENCHMARKS_DATASETS;

int round_index;

int read_cache_to_file(char* filename) {

    int outfile;
    int dumpcache_fd;
    int bytes_to_write = 0;
    struct cache_set* cache_contents;

    char csv_file_buf[WRITE_SIZE + 2*CSV_LINE_SIZE];
    int cache_set_idx, cache_line_idx;

    cache_contents = NULL;

    if (((outfile = open(filename, O_CREAT | O_WRONLY | O_SYNC | O_TRUNC, 0666)) < 0)) {
        perror("Failed to open outfile");
        exit(1);
    }

    if (!cache_contents) {
        cache_contents = (struct cache_set*) malloc(sizeof(struct cache_set) * NUM_CACHESETS);
    }

    if (((dumpcache_fd = open(PROC_FILENAME, O_RDONLY)) < 0)) {
        perror("Failed to open proc file");
        exit(1);
    }

    if (read(dumpcache_fd, cache_contents, sizeof(struct cache_set) * NUM_CACHESETS) < 0) {
        perror("Failed to read from proc file");
        exit(1);
    }

    for (cache_set_idx = 0; cache_set_idx < NUM_CACHESETS; cache_set_idx++) {
        for (cache_line_idx = 0; cache_line_idx < NUM_CACHELINES; cache_line_idx++) {

        	int d = sprintf(csv_file_buf + bytes_to_write, "%05d,0x%012lx\n",
                    cache_contents[cache_set_idx].cachelines[cache_line_idx].pid,
                    cache_contents[cache_set_idx].cachelines[cache_line_idx].addr);
        	//printf("d is: %d\r\n", d);
            bytes_to_write += d;
            //bytes_to_write += CSV_LINE_SIZE;
            if (bytes_to_write >= WRITE_SIZE){
            	//printf("On line 84, bytes_to_write is %d\r\n", bytes_to_write);
            	if (write(outfile, csv_file_buf, bytes_to_write) == -1) {
        			perror("Failed to write to outfile");
    			}
    			bytes_to_write = 0;
            }
        }   
    }

    if (bytes_to_write){
    	// Write remaining amount
    	printf("%s%d\n", "Bytes to write:", bytes_to_write);
    	if (write(outfile, csv_file_buf, bytes_to_write) == -1) {
			perror("Failed to write to outfile");
		}
	}

    close(outfile);
    close(dumpcache_fd);
    return 0;
}

void dump_proc_map(pid_t* proc_ids, int argc) {
	printf("Dumping: %d\r\n", argc);
    int i = 0;
    char cbuf[100];
    for(i = 0; i < argc; i++) {
        sprintf(cbuf, "cat /proc/%d/maps > /tmp/dumpcache/%d-%d.txt", proc_ids[i], proc_ids[i], round_index);
        system(cbuf);
    }
}

void dumpcache(void) {
	char filename[100];

	if (round_index == 0) {
		// Make folder to be used as scratch space on first iteration
		struct stat st = {0};
		if (stat(SCRATCHSPACE_DIR, &st) == -1) {
			mkdir(SCRATCHSPACE_DIR, 0700);
		}
	}
	printf("Dumping cache to CSV files (round: %d)\n", round_index);
	fflush(stdout);

	sprintf(filename, "%s/cachedump%d.csv", SCRATCHSPACE_DIR, round_index++);
    read_cache_to_file(filename);
}

int main(int argc, char* argv[]) {

	char command[200];
	char cbuf[100];
	int finished;
	short i = 0;
	short j = 0;
	pid_t cpid;
	short ctr = 0;
	pid_t* cpid_arr;
	char* executable;
	int wstatus;


	cpid_arr = malloc(sizeof(pid_t) * NUM_TOTAL_EXECUTABLES);

	// Enumerate all benchmark paths
	for(i = 0; i < NUM_SD_VBS_BENCHMARKS; i++) {
		for(j = 0; j < NUM_SD_VBS_BENCHMARKS_DATASETS; j++) {

			// If i is 0 or 1 (disparity or mser, run vga)
			// If i is 2 or 3 (sift or tracking, run cif)

			if(i == 0 || i == 1) {
				sprintf(command, "%s%s/data/%s/%s",
				BASE_SD_VBS_PATH, list_of_benchmarks[i], list_of_datasets[0], list_of_benchmarks[i]);				
			} else if(i == 2 || i == 3) {
				sprintf(command, "%s%s/data/%s/%s",
				BASE_SD_VBS_PATH, list_of_benchmarks[i], list_of_datasets[1], list_of_benchmarks[i]);
			}

			// sprintf(command, "%s%s/data/%s/%s",
			// 	BASE_SD_VBS_PATH, list_of_benchmarks[i], list_of_datasets[j], list_of_benchmarks[i]);

			printf("Running: %s\r\n", command);

			// For the current proc - set priority
			struct sched_param sp = { .sched_priority = 99 };
			int ret;
			ret = sched_setscheduler(0, SCHED_FIFO, &sp);
			sp.sched_priority = 98;
			if (ret == -1) {
				perror("sched_setscheduler");
				return 1;
			}

			cpid = fork();
			if (cpid == -1) {
				perror("fork");
				exit(EXIT_FAILURE);
			}
			if (cpid == 0) {   
				// For subsequent procs, decrement priority
				sp.sched_priority -= 1;
				ret = sched_setscheduler(getpid(), SCHED_FIFO, &sp);
				if(i == 0 || i == 1) {sprintf(cbuf, "%s%s/data/%s/", BASE_SD_VBS_PATH,
					list_of_benchmarks[i], list_of_datasets[0]);}
				else if(i == 2 || i == 3) {sprintf(cbuf, "%s%s/data/%s/", BASE_SD_VBS_PATH,
					list_of_benchmarks[i], list_of_datasets[1]);}
				
				// sprintf(cbuf, "%s%s/data/%s/", BASE_SD_VBS_PATH,
				// 	list_of_benchmarks[i], list_of_datasets[j]);
				execl(command, command, cbuf, (char *)0);
				exit(EXIT_FAILURE);
			}
			else {
				cpid_arr[i*NUM_SD_VBS_BENCHMARKS_DATASETS+j] = cpid;
			}

		}
	}


	finished = 0;
	while (!finished) {
		finished = 1;
		/* Sample the cache and write to file */
		for (ctr = 0; ctr < NUM_TOTAL_EXECUTABLES; ctr++) {
			kill(cpid_arr[ctr], SIGSTOP);
		}
		dumpcache();
    	dump_proc_map(cpid_arr, NUM_TOTAL_EXECUTABLES);
		for (ctr = 0; ctr < NUM_TOTAL_EXECUTABLES; ctr++) {
			kill(cpid_arr[ctr], SIGCONT);
		}
		/* if wstatus isn't modified by waitpid(), WIFEXITED() will
		report an incorrect value. This corrects that behavior */
		wstatus = 1;

		/* Check if all experiment processes have completed */
		for (ctr = 0; ctr < NUM_TOTAL_EXECUTABLES; ctr++) {
			waitpid(cpid_arr[ctr], &wstatus, WNOHANG);
			if(!WIFEXITED(wstatus)) {
				finished = 0;
			}
		}
		if(!finished) {
			usleep(MILLISECONDS_BETWEEN_SAMPLES);
		}
	}


	printf("done! PID of run_experiment is %d\n", getpid());
	printf("done!\n");
	printf("pids:\n");
    FILE *fp;
    fp = fopen("/tmp/dumpcache/pids.txt", "w");
    // Write number of iterations
    fprintf(fp, "%d\n", round_index);
    // Write observer PID to pids.txt
    fprintf(fp, "%d\n", getpid());
	for (ctr=0;ctr<NUM_TOTAL_EXECUTABLES;ctr++){
        fprintf(fp, "%d\n", cpid_arr[ctr]);
		printf("[%d]\n", cpid_arr[ctr]);
	}
    fclose(fp);


	return 0;
}
