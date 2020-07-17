#include "params.h"

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
    	//printf("%s%d\n", "Bytes to write:", bytes_to_write);
    	if (write(outfile, csv_file_buf, bytes_to_write) == -1) {
			perror("Failed to write to outfile");
		}
	}

    close(outfile);
    close(dumpcache_fd);
    return 0;
}

void dump_proc_map(pid_t* proc_ids, int argc) {
    int i = 0;
    char cbuf[100];
    for(i = 0; i < argc -1; i++) {
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

	char command[100];
	char* executable;
	int finished;
	int i;
	int wstatus;
	pid_t cpid;
	pid_t* cpid_arr;

	if (argc < 2) {
		printf("Please enter a list of executables to run while sampling the cache.\n");
		exit(1);
	}

	cpid_arr = malloc(sizeof(pid_t) * (argc - 1));

	for (i = 0; i < (argc - 1); i++) { /* Start by execing all experiments */

		executable = argv[i + 1]; /* Executables start at argc[1] */
		cpid = fork();
		if (cpid == -1) {
			perror("fork");
			exit(EXIT_FAILURE);
		}
		if (cpid == 0) {   /* Code executed by child */
			sprintf(command, "./%s", executable);
			printf("command: %s\r\n", command);
			//system(command);
			execl(command, command, NULL);
			exit(EXIT_FAILURE);
		}
		else { /* Code executed by parent */
                        // If we have 2 executables, one of these will run on CPU core 0 and the other one will 
                        // run on CPU core 1
                        /*if(argc >= 3) {
                            if(i == 1) { sprintf(command,"sudo taskset -p 0 "); }
                        }*/
			cpid_arr[i] = cpid;
		}
	}

	finished = 0;
	while (!finished) {
		finished = 1;
		/* Sample the cache and write to file */
		for (i = 0; i < (argc - 1); i++) {
			kill(cpid_arr[i], SIGSTOP);
		}
		dumpcache();
        dump_proc_map(cpid_arr, argc);
		for (i = 0; i < (argc - 1); i++) {
			kill(cpid_arr[i], SIGCONT);
		}
		/* if wstatus isn't modified by waitpid(), WIFEXITED() will
		report an incorrect value. This corrects that behavior */
		wstatus = 1;

		/* Check if all experiment processes have completed */
		for (i = 0; i < (argc - 1); i++) {
			waitpid(cpid_arr[i], &wstatus, WNOHANG);
			if (!WIFEXITED(wstatus)) {
				finished = 0;
			}
		}
		if (!finished) {
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
	for (i=0;i<(argc - 1);i++){
        fprintf(fp, "%d\n", cpid_arr[i]);
		printf("[%d]\n", cpid_arr[i]);
	}
    fclose(fp);
	return 0;
}
