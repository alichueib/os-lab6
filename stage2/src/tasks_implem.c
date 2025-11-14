#include <stdio.h>

#include "tasks_implem.h"
#include "tasks_queue.h"
#include "debug.h"
#include <pthread.h>

tasks_queue_t *tqueue= NULL;


void create_queues(void)
{
    tqueue = create_tasks_queue();
}

void delete_queues(void)
{
    free_tasks_queue(tqueue);
}    

//Variable declaration:
pthread_t tids[THREAD_COUNT]; //This is used in threads pool initializing

int counter = 0; 
int nb_created_tasks = 0;
int nb_terminated_tasks = 0;
//int flag = 0; // (boolean) used to verify whether the nb of created tasks is consistent (meaning that it is the final number before calling waitall)
int terminate_all_workers = 0; // (boolean) set to 1 by main thread in the runtime_finalize(), s.t. all created threads can be terminated. The workers will be checking this val in the loop each time they wake up

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

//Conditionals
pthread_cond_t non_empty = PTHREAD_COND_INITIALIZER; // worker thread waits when buffer is empty
//pthread_cond_t non_full = PTHREAD_COND_INITIALIZER; // producer thread (main thread - in stage 1) waits when buffer is full
pthread_cond_t all_ready_done = PTHREAD_COND_INITIALIZER; // producer thread waits for all the ready tasks to be done, before enqueueing any new tasks in buffer. A thread checks if a consistent (flag is set to 1) NbCreatedTasks = NbTerminatedTasks, if so it signals the main thread to wake up and resume production.

//We should also define the routine that will be done by the worker thread
void* worker_thread_routine(){
    while(!terminate_all_workers){ // keep working until killed by main thread  (by calling runtime_finalize)
        pthread_mutex_lock(&lock);
        while(counter == 0 && !terminate_all_workers){
            //PRINT_DEBUG(100, "Worker %ld waiting for task\n", pthread_self());
            pthread_cond_wait(&non_empty,&lock); //either signalled to say that the main thread have produced something, or broadcasted to terminate the worker thread
        }

        if(terminate_all_workers){ //In case main thread called runtime_finalize()
            pthread_mutex_unlock(&lock);
            break; //or return?
        }

        //Deque a task from the buffer and execute it

        active_task = get_task_to_execute(); 

        if(active_task != NULL){
            counter--;
            pthread_mutex_unlock(&lock);

            task_return_value_t ret = exec_task(active_task);

            if (ret == TASK_COMPLETED){
                terminate_task(active_task); //We will use lock inside, rather then locking here!

                pthread_mutex_lock(&lock);
                // nb_terminated_tasks++;
                //printf("DEBUG: Task fully done. nb_created=%d, nb_terminated=%d\n", nb_created_tasks, nb_terminated_tasks);
                
                //nb_terminated_tasks++;
                if(nb_created_tasks == nb_terminated_tasks){
                    //printf("DEBUG: Signaling main thread!\n");
                    //printf("I sent it\n");
                    pthread_cond_signal(&all_ready_done);
                    // flag = 0;
                }
                pthread_mutex_unlock(&lock);
                
            }
        #ifdef WITH_DEPENDENCIES
                else{
                    pthread_mutex_lock(&lock);
                    active_task->status = WAITING;
                    pthread_mutex_unlock(&lock);

                    task_check_runnable(active_task); //for getting the possible lost wake-up, in the case of all children created already finished, while parent haven't set status to waiting
                }
        #endif

        }else{
            //printf("testingg\n");
            pthread_mutex_unlock(&lock);
        }

    }
    return NULL;
}

//Now this fct is produce() fct, not only specific to main thread, thus I should rename it from main_thread_routine, to producer?
void main_thread_routine(task_t *t){ //s.t the submit() call will execute this routine
    //But here it is not a while
    pthread_mutex_lock(&lock);

    counter++;
    nb_created_tasks++;

    //printf("\t\t\t\t\t\t\t\tDEBUG: Task fully done. nb_created=%d, nb_terminated=%d\n", nb_created_tasks, nb_terminated_tasks);

    //Enqueue task to the buffer
    //Now both main thread and workers(parents/children) will be calling dispatch, so now we must protect it using lock
    //so we can't keep lock as that will lead to a deadlock, so instead we wrap the enqueue function in the dispatch with a lock
    // We needed to protect dispatch enqueue, how how can it runs if the caller is holding the lock. so release lock then call fct, ask for lock, lock granted, enqueue is run, then unlock, then when coming back re-ask for lock  
    pthread_mutex_unlock(&lock);
    dispatch_task(t);
    pthread_mutex_lock(&lock);

    pthread_cond_signal(&non_empty);
    pthread_mutex_unlock(&lock);
}


void task_waitall_helper(void)  //this function will be executed in the main thread - thus the main thread will be waiting, etc..
{
    pthread_mutex_lock(&lock);

    while( nb_created_tasks != nb_terminated_tasks ){
        //printf("////////////////////////I am waiting//////////////////////////////////\n");
        pthread_cond_wait(&all_ready_done,&lock);
    }
    //printf("I finished waiting\n");

    //Now reset (so it is the reset that could be doing the problem)
    //counter=0; //this is 0 by def (I can remove it), also flag=0 at this instant

    nb_created_tasks=0;
    nb_terminated_tasks=0;

    pthread_mutex_unlock(&lock);
}


void terminate_workers(){
    pthread_mutex_lock(&lock);
    terminate_all_workers = 1;
    pthread_cond_broadcast(&non_empty);
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
        if(pthread_create (&tids[i], NULL, worker_thread_routine, NULL) != 0){
            fprintf(stderr,"Failed to create thread %lu\n", i);
            // return EXIT_FAILURE;
        }
    }
    return ;
}

void dispatch_task(task_t *t)
{
    pthread_mutex_lock(&lock);
    enqueue_task(tqueue, t);
    pthread_mutex_unlock(&lock);
}

task_t* get_task_to_execute(void)
{
    return dequeue_task(tqueue);
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
    pthread_mutex_lock(&lock);
    t->status = TERMINATED;
    nb_terminated_tasks++;
    
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

    pthread_mutex_lock(&lock);

    int dispatch=0; //if the condition is met, then turn true, then release lock then call dispatch, else then bool is fals, so don't call dispatch

    //pthread_mutex_lock(&lock);

    if(t->status == WAITING && t->task_dependency_done == t->task_dependency_count){ //without checking if parent's status is waiting, both multiple children could run this part of code, thus leading to re-equeueing parent twice
        t->status = READY;
        counter++; // Here the child that will re-enquue the waiting parent
        //nb_terminated_tasks++;
        dispatch = 1;
    }

                    //nb_terminated_tasks++;
                // if(nb_created_tasks == nb_terminated_tasks){
                //     //printf("DEBUG: Signaling main thread!\n");
                //     printf("I sent it\n");
                //     pthread_cond_signal(&all_ready_done);
                //     // flag = 0;
                // }
    
    pthread_mutex_unlock(&lock);
    
    if(dispatch){
        //printf("I am re-enqueing up my parent at @ = %p\n", t->parent_task);
        dispatch_task(t); //after coming back we re-lock
        pthread_mutex_lock(&lock);
        pthread_cond_signal(&non_empty);
        pthread_mutex_unlock(&lock);
    }

#endif
}
