#include <stdio.h>

#include "tasks_implem.h"
#include "tasks_queue.h"
#include "debug.h"
#include <pthread.h>

#include <stdint.h>   // for intptr_t

// tasks_queue_t *tqueue= NULL;

int counter[THREAD_COUNT];
pthread_mutex_t queue_locks[THREAD_COUNT];
pthread_cond_t  non_empty[THREAD_COUNT];
tasks_queue_t  *queues[THREAD_COUNT];


void create_queues(void)
{
    // tqueue = create_tasks_queue();
    
    for (int i = 0; i < THREAD_COUNT; i++) {
        queues[i] = create_tasks_queue();           
        pthread_mutex_init(&queue_locks[i], NULL);     
        pthread_cond_init(&non_empty[i], NULL);
        counter[i]=0;
    }
}

void delete_queues(void)
{
    // free_tasks_queue(tqueue);
    
    for (int i = 0; i < THREAD_COUNT; i++) {
        free_tasks_queue(queues[i]);
        pthread_mutex_destroy(&queue_locks[i]);
        pthread_cond_destroy(&non_empty[i]);
    }
}    

//Variable declaration:
pthread_t tids[THREAD_COUNT]; //This is used in threads pool initializing

int nb_created_tasks = 0;
int nb_terminated_tasks = 0;
int flag = 0; // (boolean) used to verify whether the nb of created tasks is consistent (meaning that it is the final number before calling waitall)
int terminate_all_workers = 0; // (boolean) set to 1 by main thread in the runtime_finalize(), s.t. all created threads can be terminated. The workers will be checking this val in the loop each time they wake up

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

//Conditionals
//pthread_cond_t non_empty = PTHREAD_COND_INITIALIZER; // worker thread waits when buffer is empty
//pthread_cond_t non_full = PTHREAD_COND_INITIALIZER; // producer thread (main thread - in stage 1) waits when buffer is full
pthread_cond_t all_ready_done = PTHREAD_COND_INITIALIZER; // producer thread waits for all the ready tasks to be done, before enqueueing any new tasks in buffer. A thread checks if a consistent (flag is set to 1) NbCreatedTasks = NbTerminatedTasks, if so it signals the main thread to wake up and resume production.

//We should also define the routine that will be done by the worker thread
void* worker_thread_routine(void *arg){
    int tid = (intptr_t)arg; //now each thread can  know what cond_array/lockS_array index to  use!

    while(!terminate_all_workers){ // keep working until killed by main thread  (by calling runtime_finalize)
        pthread_mutex_lock(&queue_locks[tid]);
        while(counter[tid] == 0 && !terminate_all_workers){
            //PRINT_DEBUG(100, "Worker %ld waiting for task\n", pthread_self());
            pthread_cond_wait(&non_empty[tid],&queue_locks[tid]); //either signalled to say that the main thread have produced something, or broadcasted to terminate the worker thread
        }

        if(terminate_all_workers){ //In case main thread called runtime_finalize()
            pthread_mutex_unlock(&queue_locks[tid]);
            break; //or return?
        }

        //Deque a task from the buffer and execute it

        active_task = get_task_to_execute(tid); 

        if(active_task != NULL){
            counter[tid]--;
            pthread_mutex_unlock(&queue_locks[tid]);

            task_return_value_t ret = exec_task(active_task);

            if (ret == TASK_COMPLETED){
                terminate_task(active_task); //We will use lock inside, rather then locking here!

                pthread_mutex_lock(&lock); //correct lock here
                nb_terminated_tasks++;
                //printf("DEBUG: Task fully done. nb_created=%d, nb_terminated=%d, flag=%d\n", nb_created_tasks, nb_terminated_tasks, flag);
                
                if(nb_created_tasks == nb_terminated_tasks && flag == 1){
                    //printf("DEBUG: Signaling main thread!\n");
                    pthread_cond_signal(&all_ready_done);
                    flag = 0;
                }
                pthread_mutex_unlock(&lock);
                
            }
        #ifdef WITH_DEPENDENCIES
                else{
                    // active_task->status = WAITING;
                    pthread_mutex_lock(&lock);
                    active_task->status = WAITING;
                    pthread_mutex_unlock(&lock);
                }
        #endif

        }else{
            pthread_mutex_unlock(&queue_locks[tid]); //Why? (It should be something related to the root?)
        }

    }
    return NULL;
}

//Now this fct is produce() fct, not only specific to main thread, thus I should rename it from main_thread_routine, to producer?
void main_thread_routine(task_t *t){ //s.t the submit() call will execute this routine
    //But here it is not a while
    pthread_mutex_lock(&lock);

    //counter++; //this is the wrong place to increment counter(as I should know what exact buffer I am enqueueing to)
    nb_created_tasks++;

    //Enqueue task to the buffer
    
    pthread_mutex_unlock(&lock);
    dispatch_task(t);
    pthread_mutex_lock(&lock);

    // pthread_cond_signal(&non_empty); //This is now done in the dispatch fct, since we need to know the tid, to know what thread to signal exactly (what cond[tid])
    pthread_mutex_unlock(&lock);
}

void task_waitall_helper(void)  //this function will be executed in the main thread - thus the main thread will be waiting, etc..
{
    pthread_mutex_lock(&lock);

    flag = 1; //This will mean that there are no longer any incrementation in the variable nb_created_tasks

    if (nb_created_tasks == nb_terminated_tasks) { // I added this after debugging (after running pi.run), since without it the main thread will be waiting a signal that never arrive, this could happen when already no more workers and nb_tasks = nb_terminated
        flag = 0;
        pthread_mutex_unlock(&lock);
        return;
    }

    //Now sleep main thread will wait until a thread (executing the last task) wakes it up
    while (flag)
    {
        pthread_cond_wait(&all_ready_done,&lock);
    }

    // //Now reset
    // counter=0; //this is 0 by def (I can remove it), also flag=0 at this instant

    nb_created_tasks=0;
    nb_terminated_tasks=0;

    pthread_mutex_unlock(&lock);
}

void terminate_workers(){
    pthread_mutex_lock(&lock);
    terminate_all_workers = 1;
    // pthread_cond_broadcast(&non_empty); //since now, each thread has its own conditional that it waits based on it
    for ( unsigned long i=0; i < THREAD_COUNT; i++){
        pthread_cond_signal(&non_empty[i]);
    }  
    pthread_mutex_unlock(&lock);

    //(Make sure if this is the correct place for calling join)
    for ( unsigned long i=0; i < THREAD_COUNT; i++){
        pthread_join (tids[i], NULL) ;
    }  
}



void create_thread_pool(void)
{
    //Create the N threads (N: THREAD_COUNT, defined in Makefile.condif):
    for ( unsigned long i=0; i< THREAD_COUNT; i++){
        if(pthread_create (&tids[i], NULL, worker_thread_routine, (void*)(intptr_t)i) != 0){
            fprintf(stderr,"Failed to create thread %lu\n", i);
            // return EXIT_FAILURE;
        }
    }
    return ;
}

void dispatch_task(task_t *t)
{
    //Okay here I use Round Robin: (And don't forget to increment the counter[tid
    static int rr = 0;
    int tid = rr % THREAD_COUNT;
    rr = (rr + 1) % THREAD_COUNT;

    pthread_mutex_lock(&queue_locks[tid]);
    enqueue_task(queues[tid], t);
    counter[tid]++;

    pthread_cond_signal(&non_empty[tid]); //this was done in main thread previously!
    pthread_mutex_unlock(&queue_locks[tid]);
}

task_t* get_task_to_execute(int tid)
{
    return dequeue_task(queues[tid]);
}

unsigned int exec_task(task_t *t)
{
    t->step++;
    t->status = RUNNING;

    PRINT_DEBUG(10, "Execution of task %u (step %u)\n", t->task_id, t->step);
    
    unsigned int result = t->fct(t, t->step);
    
    return result;
}

void terminate_task(task_t *t)
{
    pthread_mutex_lock(&lock); //why protect terminated? 
    t->status = TERMINATED;
    
    PRINT_DEBUG(10, "Task terminated: %u\n", t->task_id);

    

#ifdef WITH_DEPENDENCIES
    task_t *parent = t->parent_task; //we will increment the dep done of the child's parent
    if(parent != NULL){
        // task_t *waiting_task = t->parent_task;
        // waiting_task->task_dependency_done++;
        
        // task_check_runnable(waiting_task);
        parent->task_dependency_done++;
    }

    pthread_mutex_unlock(&lock);
    //Now same as in the dispatch lock protection, rather than holding lock, we release it, and the locks will be inside
    if (parent != NULL) {
        task_check_runnable(parent);
    }

#else // why written like that ?
    pthread_mutex_unlock(&lock);
#endif

}

void task_check_runnable(task_t *t)
{
#ifdef WITH_DEPENDENCIES
    // In this version, we can't execute this without lock to protect our critical section, but we now that calling dispatch also need locks, so if we decide if we should dispatch, then outside function we call disaptch, which will have the locks inside, the Mutual exlusion is preserved.
    // if(t->task_dependency_done == t->task_dependency_count){
    //     t->status = READY;
    //     dispatch_task(t);
    // }
    
    //Instead, lets define a boolean:

    int dispatch=0; //if the condition is met, then turn true, then release lock then call dispatch, else then bool is fals, so don't call dispatch

    pthread_mutex_lock(&lock);

    if(t->status == WAITING && t->task_dependency_done == t->task_dependency_count){ //without checking if parent's status is waiting, both multiple children could run this part of code, thus leading to re-equeueing parent twice
        t->status = READY;
        // counter++; // Here the child that will re-enquue the waiting parent
        //I removed counter++, since it is know done in the dispatch fct (the only place where we can know what counter to increment)
        dispatch = 1;
    }

    pthread_mutex_unlock(&lock);
    
    if(dispatch){
        dispatch_task(t); //after coming back we re-lock
        // pthread_mutex_lock(&lock);
        // // pthread_cond_signal(&non_empty); //also is done in dispatch fct
        // pthread_mutex_unlock(&lock);
    }

#endif
}
