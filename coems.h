#include <inttypes.h>
void emit_dynamic_addr_event(const uintptr_t base);
void emit_dynamic_lock_event(const short slot, const pthread_mutex_t* addr);
