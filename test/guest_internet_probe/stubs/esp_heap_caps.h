#pragma once
#include "Arduino.h"
#include <set>
constexpr int MALLOC_CAP_INTERNAL=1,MALLOC_CAP_8BIT=2;
inline size_t freeBytes=240000,largestBytes=150000;
inline int failAllocation=0,allocationCalls=0;
inline std::set<void*> allocations;
inline size_t heap_caps_get_free_size(int){return freeBytes;}
inline size_t heap_caps_get_largest_free_block(int){return largestBytes;}
inline void* heap_caps_malloc(size_t size,int){++allocationCalls;if(allocationCalls==failAllocation)return nullptr;void* p=std::malloc(size);allocations.insert(p);return p;}
inline void heap_caps_free(void* p){if(!p)return;if(!allocations.erase(p))throw std::runtime_error("invalid free");std::free(p);}
