/*
 * queue.c
 *
 *  Created on: feb 14, 2025
 *      Author: jason.peng
 */


#include "queue.h"

Node * Create_Node(void * data);
uint32_t Free_Node(Node * node);

/* @brief: creates a dynamically allocated Node;
 *
 * @params: data to be in node
 *
 * @return: dynamically allocated Node
 */
Node * Create_Node(void * data){
	Node * ret = malloc(sizeof(Node));
	ret->Data = data;
	ret->Next = NULL;
	return ret;
}

/*
 * @brief: free node and its data at the data ptr (not inside data ptr)
 *
 * @params: node to be freed
 *
 * @return: None
 */

uint32_t Free_Node(Node * node){
	uint32_t mem_Space = 0;
	void * temp = (void*)node->Data;
	node->Data = NULL;
	node->Next = NULL;
	mem_Space += malloc_usable_size(temp);
	mem_Space += malloc_usable_size(node);
	free(temp);
	free(node);
}



/** @brief: preps a queue and initializes values
 *
 * @params: queue object to be prepped (must be pre-initialized)
 *
 * @return: nothing - preps object in place
 */
Queue * Prep_Queue(void){
	Queue * que = malloc(sizeof(Queue));
	que->Head = NULL;
	que->Tail = NULL;
	que->Size = 0;
	return que;
}

/**
 * @brief: load up a node with data (data) into the queue (que)
 *
 * @params: queue object to load new node
 * @params: data to be loaded into new node
 *
 * @return: 1 (success) or 0 (failure)
 */

uint8_t Enqueue(Queue * que, void * data){
	Node * new_Node = Create_Node(data);
	if (new_Node != NULL){
		if (que->Size == 0){
			que->Head = new_Node;
			que->Tail = new_Node;
		}
		else {
			que->Tail->Next = new_Node;
			que->Tail = new_Node;
		}
		que->Size++;
		return 1;
	}
	else
	{
		printf("Enqueue malloc error\r\n"); // how does the printf work here?
		return 0;
	}
}


/**
 * @brief: dequeue the first item (FIFO) in the queue
 *
 * @params: queue to dequeue
 *
 * @return: data of node being freed
 */
void * Dequeue(Queue * que){
	if (que->Size > 0){
		void * return_value = (void*)que->Head->Data;

		if (que->Size == 1){
			free(que->Head);
			que->Head = NULL;
			que->Tail = NULL;
		}
		else {
			Node * traq = que->Head;
			que->Head = traq->Next;
			free(traq);
		}

		que->Size--;
		return return_value;
	} else {
		return NULL;
	}
}

/**
 * @brief: dequeue the first item in the queue and free the item as well
 *
 * @params: Queue to dequeue
 *
 * @return: None
 */
void * Dequeue_Free(Queue * que){
	if (que->Size > 0){
		Node * traq = que->Head;
		que->Head = que->Head->Next;
		Free_Node(traq);
		que->Size--;
	}
}


/**
 * @brief: peeks at the data of [index] item 
 *
 * @params: Queue to peek through
 *
 * @return: Data of queued node. For example, in scheduler, returns task struct
 */
void * Queue_Peek(Queue * que, uint32_t index){
	if (index < que->Size){
		Node * travler = que->Head;
		uint32_t counter = 0;
		for(; counter < index; counter++){
			travler = (Node *)travler->Next;
		}
		return travler->Data;
	}
	else {
		printf("Error in queue peek: index out of range\r\n");
		return NULL;
	}
}

/**
 * @brief: peeks at the NODE of [index] item
 * 
 * @params: Queue to peek through, index of item to peek at
 * 
 * @return: Node signature of queued node. For example, in scheduler, returns task struct
 *
 */
Node * Queue_Node_Peek(Queue * que, uint32_t index){
	if (index < que->Size){
		Node * travler = que->Head;
		uint32_t counter = 0;
		for(; counter < index; counter++){
			travler = (Node *)travler->Next;
		}
		return travler;
	}
	else {
		printf("Error in queue peek: index out of range\r\n");
		return NULL;
	}
}

/**
 * @brief: frees the entire Queue, nodes, and data at node->data.
 * @params: queue to free
 * @return: the amount of memory freed. 
 * Should match this with sizeof(Queue) to see if whole queue was really freed. 
 */
uint32_t Free_Queue(Queue * que){
	uint32_t mem_Use = 0;
	Node * cNode = que->Head;
	Node * toFree = que->Head;
	while (toFree != NULL){
		toFree = cNode;
		cNode = cNode->Next;
		mem_Use += Free_Node(toFree);
	}
	return mem_Use;
}

