#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../tasks.h"
#include <time.h>

unsigned int heavy_task(task_t *t, unsigned int step)
{
    int   *index_ptr = (int*)    retrieve_input(t);
    double *res      = (double*) retrieve_output(t);

    int index = *index_ptr;
    double sum = 0;

    for (long i = 0; i < 20000000; i++) {
        sum += sin(index) * tan(i % 10);
    }

    *res = sum;
    return TASK_COMPLETED;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: heavy <N>\n");
        return 1;
    }

    int N = atoi(argv[1]);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    runtime_init_with_deps();

    for (int i = 0; i < N; i++) {
        task_t *t = create_task(heavy_task);

        int *inp = (int*) attach_input(t, sizeof(int));
        *inp = i;

        double *outp = (double*) attach_output(t, sizeof(double));
        *outp = 0.0;

        submit_task(t);
    }

    task_waitall();
    runtime_finalize();

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec)
                   + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Done.\nTime measured: %.3f seconds\n", elapsed);
    return 0;
}