#ifndef RTOS_OBJECTS_H
#define RTOS_OBJECTS_H

#include "threadx_includes.h"


/*--------------------------------------------THREADS--------------------------------------------*/

//include as externs. - static will create new copy in every included file (internal linkage)
/* thread control block + stack */
/*
extern TX_THREAD      app_thread;
extern UCHAR          app_thread_stack[APP_THREAD_STACK_SIZE];
*/ // defined by header file, not here
/*--------------------------------------------THREADS--------------------------------------------*/





/*--------------------------------------------MUTEX--------------------------------------------*/
//Module: Console

/*--------------------------------------------MUTEX--------------------------------------------*/



/*--------------------------------------------SEMAPHORES--------------------------------------------*/

/*--------------------------------------------SEMAPHORES--------------------------------------------*/


/*--------------------------------------------EVENT FLAGS GROUP--------------------------------------------*/

/*--------------------------------------------EVENT FLAGS GROUP--------------------------------------------*/


/*--------------------------------------------QUEUES--------------------------------------------*/
/*--------------------------------------------QUEUES--------------------------------------------*/

/*--------------------------------------------PIPES--------------------------------------------*/
/*--------------------------------------------PIPES--------------------------------------------*/


/*--------------------------------------------MEMORY MANAGEMENT--------------------------------------------*/

extern TX_BLOCK_POOL  block_pool;
extern UCHAR          block_pool_area[TX_APP_BLOCK_SIZE * TX_APP_BLOCK_COUNT];
/*--------------------------------------------MEMORY MANAGEMENT--------------------------------------------*/


/*--------------------------------------------SOFTWARE TIMERS--------------------------------------------*/
extern TX_TIMER       periodic_timer;
/*--------------------------------------------SOFTWARE TIMERS--------------------------------------------*/



/* synchronization  
extern TX_SEMAPHORE   start_semaphore;
extern TX_MUTEX       resource_mutex;
extern TX_EVENT_FLAGS_GROUP app_events;
*/

/* communications - defined in header files for queue, pipe - not here 
extern TX_QUEUE       msg_queue;
extern ULONG          queue_storage[QUEUE_MESSAGE_COUNT];
extern TX_PIPE_SIZE        
extern UCHAR (Or ULONG?)          pipe_storage[PIPE_SIZE];
*/


/* create‑all function called by ThreadX */
void rtos_objects_create(VOID *first_unused_memory);

#endif /* RTOS_OBJECTS_H */
