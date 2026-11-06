#include "ble_sdk_lifecycle.h"
#include "ble_sdk_internal.h"
#include "sdk_lifecycle_faults.h"
#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <cstring>

extern "C" { struct lifecycle_faults faults; bool _bleLibraryInUse=false; }
namespace {
TaskFunction_t pending=nullptr;
std::jmp_buf taskYield;
bool inTask=false;
}
extern "C" BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn,const char*,uint32_t,void*,int,TaskHandle_t *handle,int) {
    if (!faults.task_result) return 0;
    pending=fn; *handle=reinterpret_cast<void*>(1); return pdPASS;
}
extern "C" TickType_t xTaskGetTickCount(void) { return faults.tick; }
extern "C" UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { assert(!task); return 2048; }
extern "C" void vTaskDelay(TickType_t ticks) {
    faults.tick+=ticks;
    if (pending && faults.tick>=faults.scheduled_at) {
        auto fn=pending; pending=nullptr; inTask=true;
        if (setjmp(taskYield)==0) fn(nullptr);
        inTask=false;
    }
}
extern "C" void lifecycle_test_host_yield(void) { assert(inTask); std::longjmp(taskYield,1); }
extern "C" void vTaskDelete(TaskHandle_t task) {
    assert(!task); ++faults.task_deletes;
    if(inTask) std::longjmp(taskYield,1);
}
extern "C" esp_bt_controller_status_t esp_bt_controller_get_status(void) { return faults.controller; }

int main(int argc,char **argv) {
    // The actual pinned Arduino reservation header's constructor runs before
    // main, preventing initArduino from irreversibly releasing BLE memory.
    assert(_bleLibraryInUse);
    assert(argc==2); const char *name=argv[1];
    faults.task_result=1; faults.scheduled_at=1;
    protocomm_t pc{}; protocomm_ble_config_t config{};
    int expectedStart=ESP_OK, expectedStop=ESP_OK;
    if(!strcmp(name,"init-failure")) { faults.init_result=ESP_FAIL; expectedStart=ESP_FAIL; }
    else if(!strcmp(name,"partial-init-failure")) { faults.init_result=ESP_FAIL; faults.init_stays_enabled=true; expectedStart=expectedStop=ESP_FAIL; }
    else if(!strcmp(name,"gatt-failure")) { faults.gatt_result=ESP_FAIL; expectedStart=ESP_FAIL; }
    else if(!strcmp(name,"name-failure")) { faults.name_result=ESP_FAIL; expectedStart=ESP_FAIL; }
    else if(!strcmp(name,"task-oom")) { faults.task_result=0; expectedStart=ESP_ERR_NO_MEM; }
    else if(!strcmp(name,"task-oom-deinit-failure")) { faults.task_result=0; faults.deinit_result=ESP_FAIL; expectedStart=expectedStop=ESP_FAIL; }
    else if(!strcmp(name,"gatt-deinit-failure")) { faults.gatt_result=ESP_ERR_NO_MEM; faults.deinit_result=ESP_FAIL; expectedStart=expectedStop=ESP_FAIL; }
    else if(!strcmp(name,"sync-failure")) { faults.sync_result=ESP_FAIL; expectedStart=ESP_FAIL; }
    else if(!strcmp(name,"running-sync-timeout")) { faults.sync_result=-999; expectedStart=ESP_ERR_TIMEOUT; }
    else if(!strcmp(name,"late-start")) { faults.scheduled_at=2500; expectedStart=ESP_ERR_TIMEOUT; }
    else if(!strcmp(name,"never-scheduled")) { faults.scheduled_at=100000; expectedStart=expectedStop=ESP_ERR_TIMEOUT; }
    else if(!strcmp(name,"entered-before-loop-timeout")) { faults.delay_before_loop=true; expectedStart=expectedStop=ESP_ERR_TIMEOUT; }
    else if(!strcmp(name,"stop-failure")) { faults.stop_result=ESP_FAIL; expectedStop=ESP_FAIL; }
    else if(!strcmp(name,"deinit-failure")) { faults.deinit_result=ESP_FAIL; expectedStop=ESP_FAIL; }
    else if(!strcmp(name,"controller-still-enabled")) { faults.deinit_stays_enabled=true; expectedStop=ESP_ERR_INVALID_STATE; }
    else assert(!strcmp(name,"healthy"));

    assert(csp_ble_sdk_idle());
    const int start=csp_ble_sdk_start(&pc,&config);
    assert(start==expectedStart);
    assert(csp_ble_sdk_ready()==(expectedStart==ESP_OK));
    if(expectedStart!=ESP_OK) lifecycle_test_late_sync();
    assert(csp_ble_sdk_ready()==(expectedStart==ESP_OK));
    int stop=ESP_ERR_INVALID_ARG;
    if(!csp_ble_sdk_idle()) stop=csp_ble_sdk_stop(&pc);
    if(expectedStart==ESP_OK || !csp_ble_sdk_idle()) assert(stop==expectedStop);
    if(expectedStop==ESP_OK) assert(csp_ble_sdk_idle());
    else {
        assert(!csp_ble_sdk_idle()); assert(faults.cleanup_calls==0);
        assert(faults.gatt_free_calls==0);
    }
    assert(!csp_ble_sdk_ready());
    if(!strcmp(name,"task-oom") || !strcmp(name,"task-oom-deinit-failure")) assert(faults.host_body_calls==0);
    if(!strcmp(name,"late-start")) assert(faults.host_body_calls==0 && faults.task_deletes==1);
    if(!strcmp(name,"never-scheduled")) assert(faults.host_body_calls==0 && faults.deinit_calls==0 && faults.tick==3000);
    if(!strcmp(name,"stop-failure")) assert(faults.deinit_calls==0);
    if(!strcmp(name,"entered-before-loop-timeout")) {
        assert(faults.host_body_calls==1 && faults.deinit_calls==0 && faults.tick==3000);
        // Deterministic stale-allowed interleaving: the task observed admission,
        // paused before its loop CAS, and resumes after timeout/stop revocation.
        assert(faults.observed_loop_allowed);
        assert(!csp_ble_sdk_commit_host_loop());
        assert(!csp_ble_sdk_enter_host_loop());
        csp_ble_sdk_host_task_exit();
        assert(!csp_ble_sdk_idle() && !csp_ble_sdk_ready());
    }
    if(expectedStop!=ESP_OK) {
        const auto deinitCalls=faults.deinit_calls;
        assert(csp_ble_sdk_stop(&pc)==expectedStop);
        assert(faults.deinit_calls==deinitCalls); // no destructive retry
    }
    std::printf("%s: start=%d stop=%d ready=%d idle=%d host_bodies=%u deinit=%u cleanup=%u ticks=%u\n",name,start,stop,csp_ble_sdk_ready(),csp_ble_sdk_idle(),faults.host_body_calls,faults.deinit_calls,faults.cleanup_calls,faults.tick);
}
