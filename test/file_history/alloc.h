#pragma once
#include <cstddef>
void* history_test_alloc(size_t);
void history_test_free(void*);
#define CSP_HISTORY_ALLOC history_test_alloc
#define CSP_HISTORY_FREE history_test_free
