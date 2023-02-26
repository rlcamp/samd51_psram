#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

void psram_init(void);

/* each of these takes a pointer to a size_t, which will be incremented within an interrupt handler
 when the transaction is complete. the caller can safely __WFI() until the pointed-to variable
 reaches or exceeds the expected value */

/* this function will not block as long as not more than one transaction (read or write) is already
 in progress. it is safe to call this from an interrupt as long as the interrupt fires not more
 frequently than the expected length of the write and of any already-in-progress read */
void psram_write(const void * data, unsigned long address, size_t size, size_t * increment_when_done_p);

/* this function will not block unless a transaction (read or write) is already in progress. it is
 not safe to call this from within an interrupt */
void psram_read(void * data, unsigned long address, size_t size, size_t * increment_when_done_p);

#ifdef __cplusplus
}
#endif
