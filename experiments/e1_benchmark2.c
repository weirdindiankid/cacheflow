#include "params.h"

int main() {
	char *buf;
	int i, a;
	int iterations = 0;
	buf = (char*) malloc(BASE_BUFFSIZE_MB * 1024 * 1024);
	while (iterations < NUM_ITERATIONS) {
		// Write to 1 mb buffer
		//printf("I want to write %.2f of the buffer\r\n", ((float)iterations+1)/NUM_ITERATIONS);
		//BASE_BUFFSIZE_MB * 1024 * 1024
		for (i = 0; i < (((float)iterations+1)/NUM_ITERATIONS) * (BASE_BUFFSIZE_MB * 1024 * 1024); i = i + 4) {
			buf[i]     = '\xFE';
			buf[i + 1] = '\xCA';
			buf[i + 2] = '\xEF';
			buf[i + 3] = '\xBE';
		}
		// Read from buffer
		for (i = 0; i < (((float)iterations+1)/NUM_ITERATIONS) * (BASE_BUFFSIZE_MB * 1024 * 1024); i = i + 4) {
			a = buf[i] + buf[i + 1]  + buf[i + 2] + buf[i + 3];
		}
		// usleep(1000);
		iterations++;
	}
	free(buf);
	return 0;
}



// #include "params.h"

// /*
// Read 1/3 buf first
// Then 2/3 buf
// Then all of the buffer
// */

// int main() {
// 	char *buf;
// 	int i, a;
// 	int iterations = 0;
// 	buf = (char*) malloc((BASE_BUFFSIZE_MB / 4) * 1024 * 1024);
// 	while (iterations < (NUM_ITERATIONS * 4)) {
// 		// Write to 1 mb buffer
// 		for (i = 0; i < ((iterations + 1)/(BASE_BUFFSIZE_MB)) * (BASE_BUFFSIZE_MB / 4) * 1024 * 1024; i = i + 4) {
// 			buf[i]     = '\xFE';
// 			buf[i + 1] = '\xCA';
// 			buf[i + 2] = '\xEF';
// 			buf[i + 3] = '\xBE';
// 		}
// 		// Read from buffer
// 		for (i = 0; i < ((iterations + 1)/(BASE_BUFFSIZE_MB)) * (BASE_BUFFSIZE_MB / 4) * 1024 * 1024; i = i + 4) {
// 			a = buf[i] + buf[i + 1]  + buf[i + 2] + buf[i + 3];
// 		}
// 		// usleep(1000);
// 		iterations++;
// 	}
// 	free(buf);
// 	return 0;
// }
