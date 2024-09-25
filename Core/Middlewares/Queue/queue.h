/*
 *
 * queue.h
 *
 *  Created on: Sep 14, 2024
 *      Author: jason.peng
 */

#ifndef QUEUE_QUEUE_H_
#define QUEUE_QUEUE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>


typedef struct {
	void * Data;
	void * Next;
} Node;

typedef struct {
	Node * Head;
	Node * Tail;
	uint32_t Size;
} Queue;

Node * Create_Node(void * data);
void Prep_Queue(Queue * que);
uint8_t Enqeueue(Queue * que, void * data);
void * Dequeue(Queue * que);
void Dequeue_Free(Queue * que);
void * Queue_Peek(Queue * que, uint32_t index);
Node * Node_Peek(Queue * que, uint32_t index);
uint32_t Free_Queue(Queue * que);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_QUEUE_H_ */
