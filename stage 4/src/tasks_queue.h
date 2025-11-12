#ifndef __TASKS_QUEUE_H__
#define __TASKS_QUEUE_H__

#include "tasks.h"


typedef struct tasks_queue{
    task_t** task_buffer;
    unsigned int task_buf_size;
    unsigned int index;
    unsigned int head; //used for stealing (fifo)
    unsigned int tail; //same usage as index (lifo)
} tasks_queue_t;
    

tasks_queue_t* create_tasks_queue(void);
void free_tasks_queue(tasks_queue_t *q);

void enqueue_task(tasks_queue_t *q, task_t *t);
task_t* dequeue_task_lifo(tasks_queue_t *q);
task_t* dequeue_task_fifo(tasks_queue_t *q);
#endif
