/*
 *
 * queue.h
 *
 *  Created on: feb 14, 2025
 *      Author: jason.peng
 */

#ifndef QUEUE_QUEUE_H_
#define QUEUE_QUEUE_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "middlewares_includes.h"

typedef struct Queue Queue;

typedef struct Node {
	void * Data;
	struct Node * Next;
} Node;

typedef struct {
	Node * Head;
	Node * Tail;
	uint32_t Size;
	TX_MUTEX Lock;
} Queue;

Queue * Prep_Queue(void);
bool Enqueue(Queue * que, void * data);
void * Dequeue(Queue * que);
bool  Dequeue_Free(Queue * que);
void * Queue_Peek(Queue * que, uint32_t index);
Node * Queue_Node_Peek(Queue * que, uint32_t index);
void * Queue_Peek_Unsafe(Queue * que, uint32_t index);
Node * Queue_Node_Peek_Unsafe(Queue * que, uint32_t index);
bool Free_Queue(Queue * que);
TX_MUTEX * Queue_Get_Mutex(Queue * que);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_QUEUE_H_ */
