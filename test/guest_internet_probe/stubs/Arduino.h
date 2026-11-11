#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <functional>
#include <stdexcept>
using BaseType_t=int;
using StackType_t=uint8_t;
struct StaticTask_t {int value=0;};
struct TestTask {bool suspended=false;};
using TaskHandle_t=TestTask*;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
inline TestTask mainTask,workerTask,otherTask;
inline TaskHandle_t currentTask=&mainTask;
inline int currentCore=0;
inline uint32_t testMillis=0;
inline bool createTaskFail=false;
inline void (*taskEntry)(void*)=nullptr;
inline void* taskArgument=nullptr;
inline unsigned deletedTasks=0;
inline std::function<void()> delayHook;
struct TaskSuspended {};
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return currentTask;}
inline int xPortGetCoreID(){return currentCore;}
inline int xTaskGetCoreID(TaskHandle_t t){return t==&otherTask?1:0;}
inline uint32_t millis(){return testMillis;}
inline uint32_t pdMS_TO_TICKS(uint32_t n){return n;}
inline void vTaskDelay(uint32_t n){testMillis+=n;if(delayHook)delayHook();}
inline TaskHandle_t xTaskCreateStaticPinnedToCore(void(*fn)(void*),const char* name,size_t bytes,void* arg,int priority,StackType_t*,StaticTask_t*,int core){
 if(createTaskFail)return nullptr;
 if(std::strcmp(name,"GuestInternet")||bytes!=16384||priority!=1||core!=0)throw std::runtime_error("unexpected task admission");
 taskEntry=fn;taskArgument=arg;workerTask.suspended=false;return &workerTask;
}
inline void vTaskSuspend(void*){workerTask.suspended=true;throw TaskSuspended{};}
constexpr int eSuspended=2,eRunning=1;
inline int eTaskGetState(TaskHandle_t t){return t->suspended?eSuspended:eRunning;}
inline void vTaskDelete(TaskHandle_t t){if(t!=&workerTask||!t->suspended)throw std::runtime_error("deleted live task");++deletedTasks;taskEntry=nullptr;}
inline void runTask(){auto saved=currentTask;currentTask=&workerTask;try{taskEntry(taskArgument);}catch(const TaskSuspended&){}currentTask=saved;}

constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=0x101,ESP_ERR_INVALID_SIZE=0x104,ESP_ERR_TIMEOUT=0x107;
