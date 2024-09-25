#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

// Simulating UART_Add_Transmit function
static void test_transmit(void* UART_Handler, const char* tx_Data, size_t data_size) {
    // Simulated UART transmission logic
    printf("Transmitting over UART: %s\n", tx_Data);
}

// Static UART handler (replace with actual UART handler)
static void* UART_Handler = NULL;  // Example UART handler

// Function to handle both plain strings and formatted strings
static void UART_Transmit(const char* format, ...) {
    va_list args;

    const char* percent_sign = strchr(format, '%');

    if (percent_sign == NULL) {
        // No format specifiers found, treat as a simple string
        test_transmit(UART_Handler, format, strlen(format));
    } else {
        // Format specifiers found, treat it as a formatted string
        va_start(args, format);

        // Determine the size of the formatted string
        int needed_size = vsnprintf(NULL, 0, format, args) + 1;  // +1 for null terminator
        
        // Allocate a buffer for the formatted string
        char* buffer = (char*)malloc(needed_size);
        if (buffer == NULL) {
            va_end(args); // Clean up
            return;       // Handle memory allocation failure
        }

        // Format the string into the buffer
        vsnprintf(buffer, needed_size, format, args);

        va_end(args); // Clean up the va_list

        // Transmit the formatted string over UART
        test_transmit(UART_Handler, buffer, strlen(buffer));

        // Free the allocated buffer
        free(buffer);
    }
}
/**
int main() {
    // Example 1: Passing a simple string (no formatting)
    const char a[] = "The quick brown fox jumps over the lazy dog";
    UART_Transmit(a);  // Should handle as a plain string

    // Example 2: Passing a formatted string
    UART_Transmit("The quick brown fox %d jumps over %d lazy dogs", 1, 2);  // Formatted string

    return 0;
}
*/