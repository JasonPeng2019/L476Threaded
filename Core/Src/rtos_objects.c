#include "rtos_objects.h"

// Define the block pools, thread, and queue
TX_BLOCK_POOL tx_app_block_pool;
TX_BLOCK_POOL tx_app_mid_block_pool;
TX_BLOCK_POOL tx_app_large_block_pool;
TX_THREAD tx_app_thread;
TX_QUEUE tx_app_queue;

// Define storage areas for block pools
UCHAR tx_app_block_pool_area[TX_APP_BLOCK_SIZE * TX_APP_BLOCK_COUNT];
UCHAR tx_app_mid_block_pool_area[TX_APP_MID_BLOCK_SIZE * TX_APP_MID_BLOCK_COUNT];
UCHAR tx_app_large_block_pool_area[TX_APP_LARGE_BLOCK_SIZE * TX_APP_LARGE_BLOCK_COUNT];

// Define thread stack
UCHAR tx_app_thread_stack[TX_APP_THREAD_STACK_SIZE];

// Define queue storage
ULONG tx_app_queue_storage[10];

static UINT Safe_Block_Allocate(TX_BLOCK_POOL *pool, VOID **block_ptr, ULONG wait_option)
{
    if (!pool || !block_ptr) {
        return TX_PTR_ERROR;
    }
    return tx_block_allocate(pool, block_ptr, wait_option);
}

static UINT Safe_Block_Release(VOID *block_ptr)
{
    if (!block_ptr) {
        return TX_PTR_ERROR;
    }
    return tx_block_release(block_ptr);
}


void rtos_objects_create(VOID *first_unused_memory){
    tx_block_pool_create(&tx_app_block_pool, "Block Pool", TX_APP_BLOCK_SIZE, tx_app_block_pool_area, TX_APP_BLOCK_COUNT);
    tx_block_pool_create(&tx_app_mid_block_pool, "Mid Block Pool", TX_APP_MID_BLOCK_SIZE, tx_app_mid_block_pool_area, TX_APP_MID_BLOCK_COUNT);
    tx_block_pool_create(&tx_app_large_block_pool, "Large Block Pool", TX_APP_LARGE_BLOCK_SIZE, tx_app_large_block_pool_area, TX_APP_LARGE_BLOCK_COUNT);

    tx_thread_create(&tx_app_thread, "App Thread", app_thread_entry, NULL, tx_app_thread_stack, TX_APP_THREAD_STACK_SIZE, 1, 1, TX_NO_TIME_SLICE, TX_AUTO_START);
    // put threads here in initialization
}

/**
void app_thread_entry(ULONG thread_input)
{
    (void)thread_input;
    while (1)
    {
        // Main application thread logic here
        tx_thread_sleep(100);
    }
}
*/










