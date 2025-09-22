/* queue.c
 *
 *  Created on: feb 14, 2025
 *      Author: jason.peng
 *  Updated on: sep 17, 2025
 *      Author: buh07
 */

 #include "queuezephyr.h"
#include <string.h>

/* Dynamic allocation helpers using Zephyr allocation APIs */
static Node *Create_Node(void *data, size_t data_size) {
    Node *node = (Node *)k_malloc(sizeof(Node));
    if (!node) {
        return NULL;
    }
    memset(node, 0, sizeof(Node));

    if (data && data_size > 0) {
        node->Data = k_malloc(data_size);
        if (!node->Data) {
            k_free(node);
            return NULL;
        }
        memcpy(node->Data, data, data_size);
        node->DataSize = data_size;
    } else {
        node->Data = NULL;
        node->DataSize = 0;
    }
    node->Next = NULL;
    return node;
}

static bool Free_Node(Node *node) {
    if (!node) return true;
    if (node->Data) {
        k_free(node->Data);
    }
    k_free(node);
    return true;
}

/* Initialize a dynamically-allocated queue. Allocates a mutex for the queue. */
Queue *Prep_Queue(void) {
    Queue *que = (Queue *)k_malloc(sizeof(Queue));
    if (!que) return NULL;

    /* Initialize fields individually to avoid zeroing mutex */
    que->Head = NULL;
    que->Tail = NULL;
    que->Size = 0;
    /* Initialize embedded mutex and point Lock at it by default */
    k_mutex_init(&que->LockObj);
    que->Lock = &que->LockObj;
    return que;
}

/* Initialize a statically-allocated queue that uses a pre-allocated mutex */
void Queue_Init_Static(Queue *que, struct k_mutex *mutex) {
    if (!que) return;
    que->Head = NULL;
    que->Tail = NULL;
    que->Size = 0;
    if (mutex) {
        /* Use external mutex */
        que->Lock = mutex;
        } else {
        /* Initialize the embedded mutex */
        k_mutex_init(&que->LockObj);
        que->Lock = &que->LockObj;
        }
}

/* Initialize an existing (stack/static) Queue object */
void Queue_Init(Queue *que) {
    if (!que) return;
    que->Head = NULL;
    que->Tail = NULL;
    que->Size = 0;
    k_mutex_init(&que->LockObj);
    que->Lock = &que->LockObj;
}
 
 /**
  * @brief: enqueue data into the queue (creates a copy of the data)
  *
  * @params: que pointer to Queue, data to enqueue, data_size size of data in bytes
  *
  * @return: true on success, false on failure (NULL queue, memory allocation failure)
  */
bool Enqueue(Queue *que, void *data, size_t data_size) {
    if (!que) return false;
    if (!data && data_size > 0) return false;  /* Invalid: NULL data with non-zero size */

    /* Create node outside lock to minimize time in critical section */
    Node *node = Create_Node(data, data_size);
    if (!node) return false;

    if (que->Lock) {
        k_mutex_lock(que->Lock, K_FOREVER);
    }

    /* The queue is protected by the lock we just acquired, so it's safe to proceed */

    if (que->Size == 0) {
        que->Head = node;
        que->Tail = node;
    } else {
        que->Tail->Next = node;
        que->Tail = node;
    }
    que->Size++;

    if (que->Lock) {
        k_mutex_unlock(que->Lock);
    }
    return true;
}
 
 /**
  * @brief: dequeue data from the queue
  *
  * @params: que pointer to Queue, data_size pointer to store size of returned data
  *
  * @return: data pointer or NULL if empty (caller owns data and must free it)
  */
void *Dequeue(Queue *que, size_t *data_size) {
    if (!que) {
        if (data_size) *data_size = 0;
        return NULL;
    }

    if (que->Lock) {
        k_mutex_lock(que->Lock, K_FOREVER);
    }

    if (que->Size == 0) {
        if (que->Lock) k_mutex_unlock(que->Lock);
        if (data_size) *data_size = 0;
        return NULL;
    }

    Node *node = que->Head;
    void *data = node->Data;
    size_t size = node->DataSize;

    que->Head = node->Next;
    if (--que->Size == 0) que->Tail = NULL;

    if (que->Lock) k_mutex_unlock(que->Lock);

    if (data_size) *data_size = size;

    /* Clear node data pointer to prevent double-free, then free node */
    node->Data = NULL;
    k_free(node);
    return data;
}
 
 /**
  * @brief: dequeue and free both the data and node
  *
  * @params: que pointer to Queue
  *
  * @return: true if an element was freed, false if queue was empty or NULL
  */
bool Dequeue_Free(Queue *que) {
    size_t data_size;
    void *data = Dequeue(que, &data_size);
    if (!data) return false;
    k_free(data);
    return true;
}
 
 /**
  * @brief: peek at data at given index without removing (copies data to buffer)
  *
  * @params: que pointer to Queue, index of element, dest_buffer destination buffer,
  *          buffer_size size of destination buffer, actual_size returns actual data size
  *
  * @return: true on success, false on failure (invalid params, index out of range, buffer too small)
  */
bool Queue_Peek(Queue *que, uint32_t index, void *dest_buffer, size_t buffer_size, size_t *actual_size) {
    if (!que || !dest_buffer) return false;

    if (que->Lock) k_mutex_lock(que->Lock, K_FOREVER);

    if (index >= que->Size) {
        if (que->Lock) k_mutex_unlock(que->Lock);
        return false;
    }

    Node *trav = que->Head;
    for (uint32_t i = 0; i < index; ++i) {
        if (!trav || !trav->Next) {
            /* Queue was modified during traversal - should not happen with proper locking */
            if (que->Lock) k_mutex_unlock(que->Lock);
            return false;
        }
        trav = trav->Next;
    }

    if (!trav) {
        /* Node is NULL - queue was corrupted */
        if (que->Lock) k_mutex_unlock(que->Lock);
        return false;
    }

    if (actual_size) {
        *actual_size = trav->DataSize;
    }

    if (trav->DataSize > buffer_size) {
        if (que->Lock) k_mutex_unlock(que->Lock);
        return false;  /* Buffer too small */
    }

    if (trav->Data && trav->DataSize > 0) {
        memcpy(dest_buffer, trav->Data, trav->DataSize);
    }

    if (que->Lock) k_mutex_unlock(que->Lock);
    return true;
}

 /**
  * @brief: get the size of data at given index without removing
  *
  * @params: que pointer to Queue, index of element
  *
  * @return: size of data in bytes, 0 if invalid index or queue
  */
size_t Queue_Peek_Size(Queue *que, uint32_t index) {
    if (!que) return 0;

    if (que->Lock) k_mutex_lock(que->Lock, K_FOREVER);

    if (index >= que->Size) {
        if (que->Lock) k_mutex_unlock(que->Lock);
        return 0;
    }

    Node *trav = que->Head;
    for (uint32_t i = 0; i < index; ++i) {
        if (!trav || !trav->Next) {
            /* Queue was modified during traversal - should not happen with proper locking */
            if (que->Lock) k_mutex_unlock(que->Lock);
            return 0;
        }
        trav = trav->Next;
    }

    if (!trav) {
        /* Node is NULL - queue was corrupted */
        if (que->Lock) k_mutex_unlock(que->Lock);
        return 0;
    }

    size_t size = trav->DataSize;

    if (que->Lock) k_mutex_unlock(que->Lock);
    return size;
}
 
 /**
  * @brief: frees the entire Queue, nodes, and data at node->Data.
  *
  * @params: que pointer to Queue
  *
  * @return: true on success
  
  */
bool Free_Queue(Queue *que) {
    if (!que) return false;

    /* Free all remaining data in the queue */
    while (Dequeue_Free(que)) {
        /* Dequeue_Free handles both dequeue and free operations */
    }

    /* Embedded mutex (LockObj) is freed with the queue structure itself */
    k_free(que);
    return true;
}
 
/**
 * @brief: peek at data at given index without removing (UNSAFE - no mutex)
 * WARNING: Only call when you already hold the queue mutex externally via Queue_Get_Mutex()
 * WARNING: Returned pointer may become invalid if queue is modified by any thread
 * WARNING: Do not store returned pointer beyond the scope of your mutex lock
 *
 * @params: que pointer to Queue, index of element
 *
 * @return: data pointer or NULL (DO NOT free this pointer, queue owns it)
 */
void *Queue_Peek_Unsafe(Queue *que, uint32_t index) {
    Node *node = Queue_Node_Peek_Unsafe(que, index);
    return (node != NULL) ? node->Data : NULL;
}

/**
 * @brief: peeks at the NODE of [index] item (UNSAFE - no mutex)
 * WARNING: Only call when you already hold the queue mutex externally via Queue_Get_Mutex()
 * WARNING: Returned node pointer may become invalid if queue is modified by any thread
 * WARNING: Do not store returned node pointer beyond the scope of your mutex lock
 *
 * @params: que pointer to Queue, index of item to peek at
 *
 * @return: Node pointer or NULL (DO NOT modify or free this node, queue owns it)
 */
Node *Queue_Node_Peek_Unsafe(Queue *que, uint32_t index) {
    if (que == NULL) {
        return NULL;
    }
    if (index >= que->Size) {
        printk("Queue_Node_Peek_Unsafe index out of range\n");
        return NULL;
    }
    Node *trav = que->Head;
    for (uint32_t i = 0; i < index; ++i) {
        if (!trav || !trav->Next) {
            printk("Queue_Node_Peek_Unsafe: NULL node during traversal\n");
            return NULL;
        }
        trav = trav->Next;
    }
    return trav;
}

/**
 * @brief: get current number of elements in queue (thread-safe)
 *
 * @params: que pointer to Queue
 *
 * @return: number of elements in queue, 0 if queue is NULL
 */
uint32_t Queue_Size(Queue *que) {
    if (!que) return 0;

    if (que->Lock) k_mutex_lock(que->Lock, K_FOREVER);
    uint32_t size = que->Size;
    if (que->Lock) k_mutex_unlock(que->Lock);

    return size;
}

/**
 * @brief: get pointer to the queue's mutex for external locking
 *
 * @params: que pointer to Queue
 *
 * @return: pointer to the queue's mutex, or NULL if queue is NULL
 */
struct k_mutex *Queue_Get_Mutex(Queue *que) {
    if (!que) return NULL;
    return que->Lock;
}
