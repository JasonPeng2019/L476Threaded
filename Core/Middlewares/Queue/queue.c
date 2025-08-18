/* queue.c
 *
 *  Created on: feb 14, 2025
 *      Author: jason.peng
 */

 #include "queue.h"

 /* @brief: creates a dynamically allocated Node;
  *
  * @params: data to be in node
  *
  * @return: dynamically allocated Node
  */
 static Node *Create_Node(void *data) {
     Node *node = NULL;
     if (tx_block_allocate(&tx_app_block_pool, (VOID **)&node, TX_NO_WAIT) != TX_SUCCESS) {
         printd("malloc error\r\n");
         return NULL;
     }
     node->Data = data;
     node->Next = NULL;
     return node;
 }
 
 /**
  * @brief: frees a dynamically-allocated Node and its data
  *
  * @params: node to be freed
  *
  * @return: None
  */
 bool Free_Node(Node *node) {
     if (node == NULL) {
         return true;
     }
     if (node->Data != NULL) {
         if (tx_byte_release(node->Data) != TX_SUCCESS) {
             printd("Failed to release memory\r\n");
         }
     }
     if (tx_block_release(node) != TX_SUCCESS) {
         printd("Failed to release node memory\r\n");
         return false;
     }
     return true;
 }
 
 /**
  * @brief: initializes a new Queue
  *
  * @params: None
  *
  * @return: pointer to Queue or NULL on failure
  */
 Queue *Prep_Queue(void) {
     Queue *que = NULL;
     if (tx_block_allocate(&tx_app_large_block_pool, (VOID **)&que, TX_NO_WAIT) != TX_SUCCESS) {
         printd("Prep_Queue allocate error\r\n");
         return NULL;
     }
     que->Head = NULL;
     que->Tail = NULL;
     que->Size = 0;
     if (tx_mutex_create(&que->Lock, "QueueLock", TX_INHERIT) != TX_SUCCESS) {
         printd("Prep_Queue mutex_create error\r\n");
         tx_block_release(que);
         return NULL;
     }
     return que;
 }
 
 /**
  * @brief: enqueue data into the queue
  *
  * @params: que pointer to Queue, data to enqueue
  *
  * @return: true on success, false on failure
  */
 bool Enqueue(Queue *que, void *data) {
     if (que == NULL) {
         return false;
     }
     Node *node = Create_Node(data);
     if (node == NULL) {
     	 printd("Enqueue malloc error\r\n"); 
         return false;
     }
     if (tx_mutex_get(&que->Lock, TX_WAIT_FOREVER) != TX_SUCCESS) {
         printd("Enqueue mutex_get error\r\n");
         tx_block_release(node);
         return false;
     }
     if (que->Size == 0) {
         que->Head = node;
         que->Tail = node;
     } else {
         que->Tail->Next = node;
         que->Tail = node;
     }
     que->Size++;
     tx_mutex_put(&que->Lock);
     return true;
 }
 
 /**
  * @brief: dequeue data from the queue
  *
  * @params: que pointer to Queue
  *
  * @return: data pointer or NULL if empty (caller owns data)
  */
 void *Dequeue(Queue *que) {
     if (que == NULL) {
         return NULL;
     }
     if (tx_mutex_get(&que->Lock, TX_WAIT_FOREVER) != TX_SUCCESS) {
         printd("Dequeue mutex_get error\r\n");
         return NULL;
     }
     if (que->Size == 0) {
         tx_mutex_put(&que->Lock);
         return NULL;
     }
     Node *node = que->Head;
     void *data = node->Data;
     que->Head = node->Next;
     if (--que->Size == 0) {
         que->Tail = NULL;
     }
     tx_mutex_put(&que->Lock);
 
     /* Free only the node; data is returned to caller */
     if (tx_block_release(node) != TX_SUCCESS) {
         printd("tx_byte_release error\r\n");
     }
     return data;
 }
 
 /**
  * @brief: dequeue and free both the data and node
  *
  * @params: que pointer to Queue
  *
  * @return: true if an element was freed
  */
 bool Dequeue_Free(Queue *que) {
     void *data = Dequeue(que);
     if (data == NULL) {
         return false;
     }
     if (tx_byte_release(data) != TX_SUCCESS) {
         printd("tx_byte_release error\r\n");
     }
     return true;
 }
 
 /**
  * @brief: peek at data at given index without removing
  *
  * @params: que pointer to Queue, index of element
  *
  * @return: data pointer or NULL
  */
 void *Queue_Peek(Queue *que, uint32_t index) {
     Node *node = Queue_Node_Peek(que, index);
     return (node != NULL) ? node->Data : NULL;
 }
 
 /**
  * @brief: peeks at the NODE of [index] item
  *
  * @params: que pointer to Queue, index of item to peek at
  *
  * @return: Node signature of queued node
  */
 Node *Queue_Node_Peek(Queue *que, uint32_t index) {
     if (que == NULL) {
         return NULL;
     }
     if (tx_mutex_get(&que->Lock, TX_WAIT_FOREVER) != TX_SUCCESS) {
         printd("Queue_Node_Peek mutex_get error\r\n");
         return NULL;
     }
     if (index >= que->Size) {
         printd("Queue_Node_Peek index out of range\r\n");
         tx_mutex_put(&que->Lock);
         return NULL;
     }
     Node *trav = que->Head;
     for (uint32_t i = 0; i < index; ++i) {
         trav = trav->Next;
     }
     tx_mutex_put(&que->Lock);
     return trav;
 }
 
 /**
  * @brief: frees the entire Queue, nodes, and data at node->Data.
  *
  * @params: que pointer to Queue
  *
  * @return: true on success
  
  */
 bool Free_Queue(Queue *que) {
     if (que == NULL) {
         return false;
     }
     /* Drain the queue, freeing each data block */
     void *data;
     while ((data = Dequeue(que)) != NULL) {
         if (tx_byte_release(data) != TX_SUCCESS) {
             printd("tx_byte_release error\r\n");
         }
     }
     /* Delete the mutex and free the queue object */
     tx_mutex_delete(&que->Lock);
     if (tx_block_release(que) != TX_SUCCESS) {
         printd("tx_byte_release error\r\n");
     }
     return true;
 }
 
/**
 * @brief: get pointer to the queue's mutex for external locking
 *
 * @params: que pointer to Queue
 *
 * @return: pointer to the queue's mutex, or NULL if queue is NULL
 */
TX_MUTEX *Queue_Get_Mutex(Queue *que) {
    if (que == NULL) {
        return NULL;
    }
    return &que->Lock;
}
