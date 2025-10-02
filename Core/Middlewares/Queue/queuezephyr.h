/*
 * queuezephyr.h - Thread-safe FIFO queue implementation for Zephyr RTOS
 *
 *  Created on: feb 14, 2025
 *      Author: jason.peng
 *  Updated on: sep 21, 2025
 *      Author: buh07
 *
 * This implementation provides a thread-safe FIFO queue with the following features:
 * - Automatic data copying (queue owns the data)
 * - Thread-safe operations using Zephyr mutexes
 * - Memory-safe peek operations that copy data
 * - Clear memory ownership semantics
 * - Support for both dynamic and static queue allocation
 *
 * Memory Ownership:
 * - Enqueue: Queue makes a copy of provided data
 * - Dequeue: Caller receives ownership of allocated data and must free it
 * - Peek: Queue retains ownership, data is copied to caller's buffer
 */

#ifndef QUEUE_QUEUE_H_
#define QUEUE_QUEUE_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "middlewares_includes.h"
#include "includes/zrtos.h"

#define QUEUE_MUTEX_DEFINE(name) Z_MUTEX_DEFINE(name)

typedef struct Node {
	void * Data;
	size_t DataSize;
	struct Node * Next;
} Node;

typedef struct Queue {
	Node * Head;
	Node * Tail;
	uint32_t Size;
	/* Internal mutex object (embedded). Use Queue_Init or Prep_Queue to
	 * initialize. `Lock` points to the active mutex (either &LockObj or an
	 * externally-supplied mutex). */
	z_mutex_t LockObj;
	z_mutex_t *Lock; /* pointer to mutex: allows static or external mutex */
} Queue;

/* Queue allocation and initialization */
Queue * Prep_Queue(void);  /* Allocates and initializes a new queue */
void Queue_Init(Queue *que);
void Queue_Init_Static(Queue *que, z_mutex_t *mutex);

/* Core queue operations (thread-safe) */
bool Enqueue(Queue * que, void * data, size_t data_size);  /* Add data copy to rear */
void * Dequeue(Queue * que, size_t * data_size);           /* Remove data from front, caller owns result */
bool Dequeue_Free(Queue * que);                            /* Remove and free data from front */

/* Peek operations (thread-safe, copy data) */
bool Queue_Peek(Queue * que, uint32_t index, void * dest_buffer, size_t buffer_size, size_t * actual_size);
size_t Queue_Peek_Size(Queue * que, uint32_t index);       /* Get size of data at index */

/* Queue information */
uint32_t Queue_Size(Queue * que);                          /* Get current number of elements */

/* Unsafe operations (caller must hold mutex) */
void * Queue_Peek_Unsafe(Queue * que, uint32_t index);     /* WARNING: Returns pointer, not copy */
Node * Queue_Node_Peek_Unsafe(Queue * que, uint32_t index); /* WARNING: Returns node pointer */

/* Queue cleanup */
bool Free_Queue(Queue * que);  /* Free queue and all contained data */
/* Return pointer to the underlying mutex protecting the queue. Use only in
 * thread context; mutex APIs must not be called from ISRs. If ISR access is
 * required, use an ISR-safe primitive such as k_fifo/k_msgq instead.
 */
z_mutex_t * Queue_Get_Mutex(Queue * que);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_QUEUE_H_ */
