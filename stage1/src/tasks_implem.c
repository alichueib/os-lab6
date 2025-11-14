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

/*
int main(void)
{
    pthread_t tids[NB_THREADS];
    unsigned long i=0;

    for(i=0; i<NB_THREADS; i++){
        if(pthread_create (&tids[i], NULL, thread_routine, (void*)i) != 0){
            fprintf(stderr,"Failed to create thread %lu\n", i);
            return EXIT_FAILURE;
        }
    }
    
     Wait until every thread ended 

    
    return EXIT_SUCCESS;
}
*/

//What if we define all those in a seperate file? (now do it all here:)

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
pthread_cond_t non_full = PTHREAD_COND_INITIALIZER; // producer thread (main thread - in stage 1) waits when buffer is full
pthread_cond_t all_ready_done = PTHREAD_COND_INITIALIZER; // producer thread waits for all the ready tasks to be done, before enqueueing any new tasks in buffer. A thread checks if a consistent (flag is set to 1) NbCreatedTasks = NbTerminatedTasks, if so it signals the main thread to wake up and resume production.

//We should also define the routine that will be done by the worker thread
// void* worker_thread_routine(){
//     while(!terminate_all_workers){ // keep working until killed by main thread  (by calling runtime_finalize)
//         pthread_mutex_lock(&lock);
//         while(counter == 0){

//             PRINT_DEBUG(100, "Worker %ld waiting for task\n", pthread_self());
//             pthread_cond_wait(&non_empty,&lock); //either signalled to say that the main thread have produced something, or broadcasted to terminate the worker thread
//             if(terminate_all_workers){ //In case main thread called runtime_finalize()
//                 pthread_mutex_unlock(&lock);
//                 break; //or return?
//             }
//         }
//         //Since it is consumed  (so we are callin this at the wrong time, as how are we reaching count=0?)

//         //Deque a task from the buffer and execute it

//         active_task = get_task_to_execute(); 
//         counter--; //This is the correct place, but why isn't the main thread stopping when reaching queue capacity?
//         printf("Decrmented %d\n", counter);
        

//         if(active_task != NULL){
//             task_return_value_t ret = exec_task(active_task);

//             if (ret == TASK_COMPLETED){
//                 terminate_task(active_task);
//                 // nb_terminated_tasks++;
                
//                 printf("DEBUG: Task fully done. nb_created=%d, nb_terminated=%d , counter=%d\n", nb_created_tasks, nb_terminated_tasks, counter);
//             }
//         #ifdef WITH_DEPENDENCIES
//                 else{
//                     active_task->status = WAITING;
//                 }
//         #endif

//         }
//         nb_terminated_tasks++;




//         if(nb_created_tasks == nb_terminated_tasks){
//             pthread_cond_signal(&all_ready_done);
//             // flag = 0;
//         }else{
//             pthread_cond_signal(&non_full);
//         }
//         pthread_mutex_unlock(&lock);
//     }
//     return NULL;
// }

////////////////////////////////////////////////
// void* worker_thread_routine(){
//     while(!terminate_all_workers){
//         pthread_mutex_lock(&lock);
//         while(counter == 0){
//             pthread_cond_wait(&non_empty,&lock); 
//             if(terminate_all_workers){ 
//                 pthread_mutex_unlock(&lock);
//                 break; 
//             }
//         }

//         active_task = get_task_to_execute(); 
//         counter--;
                

//         if(active_task != NULL){
//             task_return_value_t ret = exec_task(active_task);

//             if (ret == TASK_COMPLETED){
//                 terminate_task(active_task);
//                 nb_terminated_tasks++;
                
//                 printf("DEBUG: Task fully done. nb_created=%d, nb_terminated=%d , counter=%d\n", nb_created_tasks, nb_terminated_tasks, counter);
//             }
//         #ifdef WITH_DEPENDENCIES
//                 else{
//                     active_task->status = WAITING;
//                 }
//         #endif

//         }




//         if(nb_created_tasks == nb_terminated_tasks){
//             pthread_cond_signal(&all_ready_done);
//             // flag = 0;
//         }else{
//             pthread_cond_signal(&non_full);
//         }
//         pthread_mutex_unlock(&lock);
//     }
//     return NULL;
// }


void* worker_thread_routine(){
    while(!terminate_all_workers){ // keep working until killed by main thread  (by calling runtime_finalize)
        pthread_mutex_lock(&lock);
        while(counter == 0){
            PRINT_DEBUG(100, "Worker %ld waiting for task\n", pthread_self());
            pthread_cond_wait(&non_empty,&lock); //either signalled to say that the main thread have produced something, or broadcasted to terminate the worker thread
            if(terminate_all_workers){ //In case main thread called runtime_finalize()
                pthread_mutex_unlock(&lock);
                break; //or return?
            }
        }

        //printf("DEBUG: Task fully done. nb_created=%d, nb_terminated=%d , counter=%d\n", nb_created_tasks, nb_terminated_tasks, counter);

        //Deque a task from the buffer and execute it

        active_task = get_task_to_execute(); 

        if(active_task != NULL){
            counter--; //Since it is consumed 
            nb_terminated_tasks++;


            task_return_value_t ret = exec_task(active_task);

            if (ret == TASK_COMPLETED){
                terminate_task(active_task);
                printf("DEBUG: Task fully done. nb_created=%d, nb_terminated=%d , counter=%d\n", nb_created_tasks, nb_terminated_tasks, counter);
            }


        }else{
            printf("Error \n");
        }

        // if(active_task != NULL){
        //     task_return_value_t ret = exec_task(active_task);

        //     if (ret == TASK_COMPLETED){
        //         terminate_task(active_task);
        //     }
        // #ifdef WITH_DEPENDENCIES
        //         else{
        //             active_task->status = WAITING;
        //         }
        // #endif

        // }



        if(nb_created_tasks == nb_terminated_tasks){
            pthread_cond_signal(&all_ready_done);
        }else{
            pthread_cond_signal(&non_full);
        }
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}


void main_thread_routine(task_t *t){ //s.t the submit() call will execute this routine
    //But here it is not a while
    pthread_mutex_lock(&lock);
    while (counter==QUEUE_CAPACITY)
    {
        pthread_cond_wait(&non_full,&lock);
    }
    counter++;
    nb_created_tasks++;

    //Enqueue task to the buffer
    dispatch_task(t);

    pthread_cond_signal(&non_empty);
    pthread_mutex_unlock(&lock);
}

void task_waitall_helper(void)  //this function will be executed in the main thread - thus the main thread will be waiting, etc..
{
    pthread_mutex_lock(&lock);

    //flag = 1; //This will mean that there are no longer any incrementation in the variable nb_created_tasks

    // if (nb_created_tasks == nb_terminated_tasks) { // I added this after debugging (after running pi.run), since without it the main thread will be waiting a signal that never arrive, this could happen when already no more workers and nb_tasks = nb_terminated
    //     flag = 0;
    //     pthread_mutex_unlock(&lock);
    //     return;
    // }

    //Now sleep main thread will wait until a thread (executing the last task) wakes it up
    // while (flag)
    // {
    //     pthread_cond_wait(&all_ready_done,&lock);
    // }

    // if (nb_created_tasks == nb_terminated_tasks) { // I added this after debugging (after running pi.run), since without it the main thread will be waiting a signal that never arrive, this could happen when already no more workers and nb_tasks = nb_terminated
    //     pthread_mutex_unlock(&lock);
    //     return;
    // }
    // pthread_cond_wait(&all_ready_done,&lock);

    while( nb_created_tasks != nb_terminated_tasks ){
        printf("////////////////////////I am waiting//////////////////////////////////\n");
        pthread_cond_wait(&all_ready_done,&lock);
    }
    printf("I finished waiting\n");

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
    enqueue_task(tqueue, t);
    printf("I have added the task %d\n",counter);
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
    t->status = TERMINATED;

    //nb_terminated_tasks++;
    printf("worker finished working\n");
    
    PRINT_DEBUG(10, "Task terminated: %u\n", t->task_id);

#ifdef WITH_DEPENDENCIES
    if(t->parent_task != NULL){
        task_t *waiting_task = t->parent_task;
        waiting_task->task_dependency_done++;
        
        task_check_runnable(waiting_task);
    }
#endif

}

void task_check_runnable(task_t *t)
{
#ifdef WITH_DEPENDENCIES
    if(t->task_dependency_done == t->task_dependency_count){
        //nb_terminated_tasks++;
        printf("The partent has done working\n");
        t->status = READY;
        dispatch_task(t);
    }
#endif
}
