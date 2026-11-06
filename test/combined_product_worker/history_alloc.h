#pragma once
#include <cstddef>
void* combined_history_alloc(size_t);
void combined_history_free(void*);
#define CSP_HISTORY_ALLOC combined_history_alloc
#define CSP_HISTORY_FREE combined_history_free
