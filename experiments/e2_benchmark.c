#include "params.h"

void read_memory_map(void) {
	char input_filename[20];
	FILE* mmap_fp;
	FILE* output_fp;
	char s[100];

	mmap_fp = fopen("/proc/self/maps", "r");
	if (mmap_fp == NULL) {
		perror("Can't open mmap_fp");
	}
	output_fp = fopen("/tmp/dumpcache/mmap.txt", "w+");
	if (output_fp == NULL) {
		perror("Can't open output_fp");
	}

	while (fgets(s, 100, mmap_fp) != NULL) {
		fputs(s, output_fp);
	}

	fclose(mmap_fp);
	fclose(output_fp);
	return;
}

int main() {
	char *buf;
	int i, a, j;
	int iterations = 0;
	buf = (char*) malloc(BASE_BUFFSIZE_MB * 1024 * 1024);
	while (iterations < NUM_ITERATIONS) {
		j = 0;
		if (iterations > 5){
			j = (BASE_BUFFSIZE_MB * 1024 * 1024)/2;
		}
		
		for (i=j; i < BASE_BUFFSIZE_MB * 1024 * 1024; i = i + 4) {
			buf[i]     = '\xFE';
			buf[i + 1] = '\xCA';
			buf[i + 2] = '\xEF';
			buf[i + 3] = '\xBE';
		}
		// Read from buffer
		for (i=j; i < BASE_BUFFSIZE_MB * 1024 * 1024; i = i + 4) {
			a = buf[i] + buf[i + 1]  + buf[i + 2] + buf[i + 3];
		}
		// usleep(1000);
		iterations++;
	}
	
	free(buf);
	read_memory_map();
	return 0;
}
