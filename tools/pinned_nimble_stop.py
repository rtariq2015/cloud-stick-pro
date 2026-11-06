#!/usr/bin/env python3
"""Reproduce the lab5 project-side NimBLE backport; never edits the SDK."""
from pathlib import Path
import argparse,difflib,hashlib,json
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'sdk/pinned_nimble_stop'

def function(text,name):
 needle='\n'+name+'(';assert text.count(needle)==1,name
 at=text.index(needle);start=text.rfind('\n',0,at)+1
 brace=text.index('{',at);depth=1;end=brace+1
 while depth:
  if text[end]=='{':depth+=1
  if text[end]=='}':depth-=1
  end+=1
 return text[start:end]
def replace_function(text,name,replacement):
 old=function(text,name);assert text.count(old)==1
 return text.replace(old,replacement.strip(),1)

HELPERS=r'''
/* Task queue operations share one mutex; interrupt/atomic queue operations
 * share ble_port_mutex. Never wait on a mutex while inside an NPL critical
 * region. Peeking a blocking queue happens before taking either lock. */
static bool
npl_eventq_lock(bool *locked, TickType_t wait)
{
    *locked = false;
    if (in_isr() || hw_critical_state_status[xPortGetCoreID()] != 0) {
        return true;
    }
    BLE_LL_ASSERT(npl_eventq_sync);
    if (xSemaphoreTakeRecursive(npl_eventq_sync, wait) != pdTRUE) {
        return false;
    }
    *locked = true;
    return true;
}

static void
npl_eventq_unlock(bool locked)
{
    if (locked) {
        BaseType_t ret = xSemaphoreGiveRecursive(npl_eventq_sync);
        BLE_LL_ASSERT(ret == pdTRUE);
    }
}
'''
GET=r'''
struct ble_npl_event *
npl_freertos_eventq_get(struct ble_npl_eventq *evq, ble_npl_time_t tmo)
{
    struct ble_npl_event *ev = NULL;
    struct ble_npl_eventq_freertos *eventq = (struct ble_npl_eventq_freertos *)evq->eventq;
    BaseType_t ret, woken = pdFALSE;
    if (in_isr()) {
        BLE_LL_ASSERT(tmo == 0);
        portENTER_CRITICAL_ISR(&ble_port_mutex);
        ret = xQueueReceiveFromISR(eventq->q, &ev, &woken);
        if (ret == pdPASS && ev != NULL) {
            struct ble_npl_event_freertos *event = (struct ble_npl_event_freertos *)ev->event;
            BLE_LL_ASSERT(event);
            event->queued = false;
        }
        portEXIT_CRITICAL_ISR(&ble_port_mutex);
        BLE_LL_ASSERT(ret == pdPASS || ret == errQUEUE_EMPTY);
        if (woken == pdTRUE) { portYIELD_FROM_ISR(); }
        return ev;
    }
    BLE_LL_ASSERT(tmo == 0 || hw_critical_state_status[xPortGetCoreID()] == 0);
    const TickType_t started = xTaskGetTickCount();
    for (;;) {
        TickType_t remaining = tmo;
        if (tmo != 0) {
            if (tmo != portMAX_DELAY) {
                TickType_t elapsed = xTaskGetTickCount() - started;
                if (elapsed >= tmo) { return NULL; }
                remaining = tmo - elapsed;
            }
            if (xQueuePeek(eventq->q, &ev, remaining) != pdPASS) { return NULL; }
        }
        if (tmo != 0 && tmo != portMAX_DELAY) {
            TickType_t elapsed = xTaskGetTickCount() - started;
            if (elapsed >= tmo) { return NULL; }
            remaining = tmo - elapsed;
        }
        bool locked;
        if (!npl_eventq_lock(&locked, remaining)) { return NULL; }
        ev = NULL; /* A peeked event may have been removed before lock admission. */
        portENTER_CRITICAL(&ble_port_mutex);
        ret = xQueueReceive(eventq->q, &ev, 0);
        if (ret == pdPASS && ev != NULL) {
            struct ble_npl_event_freertos *event = (struct ble_npl_event_freertos *)ev->event;
            BLE_LL_ASSERT(event);
            event->queued = false;
        }
        portEXIT_CRITICAL(&ble_port_mutex);
        npl_eventq_unlock(locked);
        BLE_LL_ASSERT(ret == pdPASS || ret == errQUEUE_EMPTY);
        if (ret == pdPASS) { return ev; }
        if (tmo == 0) { return NULL; }
    }
}
'''
PUT=r'''
void
npl_freertos_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    struct ble_npl_eventq_freertos *eventq = (struct ble_npl_eventq_freertos *)evq->eventq;
    struct ble_npl_event_freertos *event = (struct ble_npl_event_freertos *)ev->event;
    BaseType_t ret = pdPASS, woken = pdFALSE;
    bool locked = false;
    if (in_isr()) {
        portENTER_CRITICAL_ISR(&ble_port_mutex);
        if (!event->queued) {
            ret = xQueueSendToBackFromISR(eventq->q, &ev, &woken);
            if (ret == pdPASS) { event->queued = true; }
        }
        portEXIT_CRITICAL_ISR(&ble_port_mutex);
        BLE_LL_ASSERT(ret == pdPASS);
        if (woken == pdTRUE) { portYIELD_FROM_ISR(); }
    } else {
        bool admitted = npl_eventq_lock(&locked, portMAX_DELAY);
        BLE_LL_ASSERT(admitted);
        portENTER_CRITICAL(&ble_port_mutex);
        if (!event->queued) {
            ret = xQueueSendToBack(eventq->q, &ev, 0);
            if (ret == pdPASS) { event->queued = true; }
        }
        portEXIT_CRITICAL(&ble_port_mutex);
        npl_eventq_unlock(locked);
        BLE_LL_ASSERT(ret == pdPASS);
    }
}
'''
REMOVE=r'''
void
npl_freertos_eventq_remove(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    struct ble_npl_eventq_freertos *eventq = (struct ble_npl_eventq_freertos *)evq->eventq;
    struct ble_npl_event_freertos *event = (struct ble_npl_event_freertos *)ev->event;
    BaseType_t ret, woken = pdFALSE, woken2 = pdFALSE;
    bool locked = false, removed = false;
    const bool isr = in_isr();
    if (isr) { portENTER_CRITICAL_ISR(&ble_port_mutex); }
    else {
        bool admitted = npl_eventq_lock(&locked, portMAX_DELAY);
        BLE_LL_ASSERT(admitted);
        portENTER_CRITICAL(&ble_port_mutex);
    }
    if (event->queued) {
        int count = isr ? uxQueueMessagesWaitingFromISR(eventq->q) : uxQueueMessagesWaiting(eventq->q);
        for (int i = 0; i < count; ++i) {
            struct ble_npl_event *other = NULL;
            ret = isr ? xQueueReceiveFromISR(eventq->q, &other, &woken2) : xQueueReceive(eventq->q, &other, 0);
            BLE_LL_ASSERT(ret == pdPASS);
            woken |= woken2;
            if (other == ev) { removed = true; continue; }
            ret = isr ? xQueueSendToBackFromISR(eventq->q, &other, &woken2) : xQueueSendToBack(eventq->q, &other, 0);
            BLE_LL_ASSERT(ret == pdPASS);
            woken |= woken2;
        }
        BLE_LL_ASSERT(removed); /* A queued event must belong to this queue. */
        event->queued = false;
    }
    if (isr) { portEXIT_CRITICAL_ISR(&ble_port_mutex); }
    else { portEXIT_CRITICAL(&ble_port_mutex); npl_eventq_unlock(locked); }
    if (isr && woken == pdTRUE) { portYIELD_FROM_ISR(); }
}
'''
QUEUED=r'''
bool
npl_freertos_event_is_queued(struct ble_npl_event *ev)
{
    struct ble_npl_event_freertos *event = (struct ble_npl_event_freertos *)ev->event;
    bool queued;
    const bool isr = in_isr();
    if (isr) { portENTER_CRITICAL_ISR(&ble_port_mutex); }
    else { portENTER_CRITICAL(&ble_port_mutex); }
    queued = event != NULL && event->queued;
    if (isr) { portEXIT_CRITICAL_ISR(&ble_port_mutex); }
    else { portEXIT_CRITICAL(&ble_port_mutex); }
    return queued;
}
'''

def patched():
 manifest=json.loads((BASE/'INPUTS.json').read_text())
 for relative,entry in manifest.items():
  assert hashlib.sha256((ROOT/relative).read_bytes()).hexdigest()==entry['sha256'],relative
 npl=(BASE/'upstream/npl_os_freertos.c').read_text()
 needle='portMUX_TYPE ble_port_mutex = portMUX_INITIALIZER_UNLOCKED;'
 assert npl.count(needle)==1
 npl=npl.replace(needle,needle+'\nstatic SemaphoreHandle_t npl_eventq_sync;\nstatic uint8_t hw_critical_state_status[portNUM_PROCESSORS];',1)
 old='uint8_t hw_critical_state_status = 0;';assert npl.count(old)==1;npl=npl.replace(old,'',1)
 old=function(npl,'in_isr');npl=npl.replace(old,old+'\n'+HELPERS,1)
 for name,code in [('npl_freertos_eventq_get',GET),('npl_freertos_eventq_put',PUT),('npl_freertos_eventq_remove',REMOVE),('npl_freertos_event_is_queued',QUEUED)]:npl=replace_function(npl,name,code)
 npl=replace_function(npl,'npl_freertos_hw_enter_critical','''uint32_t
npl_freertos_hw_enter_critical(void)
{
    portENTER_CRITICAL(&ble_port_mutex);
    BLE_LL_ASSERT(hw_critical_state_status[xPortGetCoreID()] != UINT8_MAX);
    ++hw_critical_state_status[xPortGetCoreID()];
    return 0;
}''')
 npl=replace_function(npl,'npl_freertos_hw_is_in_critical','''uint8_t
npl_freertos_hw_is_in_critical(void)
{
    return hw_critical_state_status[xPortGetCoreID()];
}''')
 npl=replace_function(npl,'npl_freertos_hw_exit_critical','''void
npl_freertos_hw_exit_critical(uint32_t ctx)
{
    (void)ctx;
    BLE_LL_ASSERT(hw_critical_state_status[xPortGetCoreID()] != 0);
    --hw_critical_state_status[xPortGetCoreID()];
    portEXIT_CRITICAL(&ble_port_mutex);
}''')
 old='int npl_freertos_mempool_init(void)\n{\n    int rc = -1;'
 assert npl.count(old)==1
 npl=npl.replace(old,old+'''\n
    if (!npl_eventq_sync) {
        npl_eventq_sync = xSemaphoreCreateRecursiveMutex();
        /* No pool has been touched yet. Report this new allocation failure to
         * the existing checked port-init caller without entering SDK asserts. */
        if (!npl_eventq_sync) { return -1; }
    }
''',1)
 old='_error:\n';assert npl.count(old)==1
 cleanup='''    if (npl_eventq_sync) {
        vSemaphoreDelete(npl_eventq_sync);
        npl_eventq_sync = NULL;
    }
'''
 npl=npl.replace(old,old+'\n'+cleanup,1)
 old='void npl_freertos_mempool_deinit(void)\n{';assert npl.count(old)==1
 npl=npl.replace(old,old+'\n'+cleanup,1)
 hs=(BASE/'upstream/ble_hs.c').read_text()
 old='#include "ble_hs_priv.h"';assert hs.count(old)==1
 hs=hs.replace(old,'#include "../host_headers/ble_hs_priv.h"',1)
 old='#include "bt_common.h"';assert hs.count(old)==1
 hs=hs.replace(old,'#include "../host_headers/bt_common.h"',1)
 old='        ble_npl_callout_stop(&ble_hs_timer);\n        ble_npl_callout_deinit(&ble_hs_timer);'
 assert hs.count(old)==1
 hs=hs.replace(old,'        ble_npl_callout_stop(&ble_hs_timer);',1)
 # Official stop-only guard also prevents a dequeued timer from rescheduling
 # after the stopping owner has retained its still-live callout.
 old='''    if (ticks_from_now == BLE_HS_FOREVER) {
        return;
    }
''';assert hs.count(old)==1
 hs=hs.replace(old,old+'''\n    if (!ble_hs_is_enabled()) {
        return;
    }
''',1)
 return {'npl_os_freertos.c':npl,'ble_hs.c':hs}

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--write',action='store_true');args=parser.parse_args()
 for name,text in patched().items():
  target=BASE/'patched'/name;upstream=(BASE/'upstream'/name).read_text()
  patch=''.join(difflib.unified_diff(upstream.splitlines(True),text.splitlines(True),fromfile='upstream/'+name,tofile='patched/'+name))
  if args.write:target.write_text(text);(BASE/(name+'.patch')).write_text(patch)
  else:assert target.read_text()==text and (BASE/(name+'.patch')).read_text()==patch,name
 print('PASS pinned inputs and exact lab5 transformations; SDK untouched')
if __name__=='__main__':main()
