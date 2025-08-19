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

extern TX_BLOCK_POOL  tx_app_block_pool;
extern UCHAR          block_pool_area[TX_APP_BLOCK_SIZE * TX_APP_BLOCK_COUNT];

// Additional block pools
extern TX_BLOCK_POOL  tx_app_mid_block_pool;
extern UCHAR          block_pool_area[TX_APP_BLOCK_SIZE * TX_APP_BLOCK_COUNT];

extern TX_BLOCK_POOL  tx_app_large_block_pool;
extern UCHAR          block_pool_area[TX_APP_BLOCK_SIZE * TX_APP_BLOCK_COUNT];


// Thread and queue
extern TX_THREAD      tx_app_thread;
extern TX_QUEUE       tx_app_queue;

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

/* Thread entry function */
void app_thread_entry(ULONG thread_input);
UINT Safe_Block_Release(VOID *block_ptr);
UINT Safe_Block_Allocate(TX_BLOCK_POOL *pool, VOID **block_ptr, ULONG wait_option);

#endif /* RTOS_OBJECTS_H */
