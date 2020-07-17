all: clean e1_benchmark e1_benchmark1 e1_benchmark2 e2_benchmark snapshot

snapshot: snapshot.c
	gcc -Wall -o e1_run_experiment e1_run_experiment.c

e1_benchmark: e1_benchmark.c
	gcc -o e1_benchmark e1_benchmark.c

e1_benchmark1: e1_benchmark1.c
	gcc -o e1_benchmark1 e1_benchmark1.c

e1_benchmark2: e1_benchmark2.c
	gcc -o e1_benchmark2 e1_benchmark2.c

e2_benchmark: e2_benchmark.c
	gcc -o e2_benchmark e2_benchmark.c

clean:
	rm -f  e1_benchmark e1_benchmark1 e1_benchmark2 e2_benchmark snapshot
