#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/* each of these takes a pointer to a volatile char, which should be initially set by the caller,
 and which will be cleared asynchronously when the framework is done reading from or writing to
 the given data pointer. the caller can then do something like "while (busy) __WFI();" if it needs
 to efficiently wait for the transaction to finish before some other operation */

void psram_init(void);

/* this function will not block as long as not more than one transaction (read or write) is already
 in progress. it is safe to call this from an interrupt as long as the interrupt fires not more
 frequently than the expected length of the write and of any already-in-progress read */
void psram_write(const void * data, unsigned long address, size_t size, volatile char * busy_p);

/* this function will not block unless a transaction (read or write) is already in progress. it is
 not safe to call this from within an interrupt */
void psram_read(void * data, unsigned long address, size_t size, volatile char * busy_p);

#ifdef __cplusplus
}
#endif
