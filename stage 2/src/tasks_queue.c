#include <stdio.h>
#include <stdlib.h>

#include "tasks_queue.h"


tasks_queue_t* create_tasks_queue(void)
{
    tasks_queue_t *q = (tasks_queue_t*) malloc(sizeof(tasks_queue_t));

    q->task_buf_size = QUEUE_CAPACITY;
    q->task_buffer = (task_t**) malloc(sizeof(task_t*) * q->task_buf_size);

    q->index = 0;

    return q;
}


void free_tasks_queue(tasks_queue_t *q)
{
    /* IMPORTANT: We chose not to free the queues to simplify the
     * termination of the program (and make debugging less complex) */
    
    /* free(q->task_buffer); */
    /* free(q); */
}


//Edit1: (Where here if the buffer of tasks is already full, rather then waiting for non-empty, we grow buffer dynamically)
void enqueue_task(tasks_queue_t *q, task_t *t)
{
    //Check if queue is full, (since index is updated then index == buff size) -not buff_size + 1, as we started with index = 0
    if (q->index == q->task_buf_size) {
        size_t new_size = q->task_buf_size * 2;
        task_t **new_buffer = realloc(q->task_buffer, sizeof(task_t*) * new_size);
        if (new_buffer == NULL) {
            fprintf(stderr, "ERROR: failed to grow the task queue\n");
            exit(EXIT_FAILURE);
        }

        q->task_buffer = new_buffer;
        q->task_buf_size = new_size;
        fprintf(stderr, "[INFO] Task queue capacity doubled to %zu\n", new_size);
    }

    q->task_buffer[q->index] = t;
    q->index++;
}


task_t* dequeue_task(tasks_queue_t *q)
{
    if(q->index == 0){
        return NULL;
    }

    task_t *t = q->task_buffer[q->index-1];
    // q->index = (q->index - 1)% q->task_buf_size;
    q->index--;


    return t;
}

