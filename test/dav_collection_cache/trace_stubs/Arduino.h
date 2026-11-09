#pragma once
#include <atomic>
#include <cstdint>
extern std::atomic<uint32_t> traceNow;
inline uint32_t millis(){return traceNow.load();}
