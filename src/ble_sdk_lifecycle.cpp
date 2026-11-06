#include "ble_feature.h"
#if CSP_ENABLE_BLE_MANAGEMENT
#include "ble_sdk_lifecycle.h"
#include "ble_sdk_internal.h"
#include <esp32-hal-alloc-ble-mem.h>
#include <esp_bt.h>
#include <nimble/nimble_port.h>
#include <atomic>

namespace {
constexpr uint32_t StartupWaitMs = 2000, ExitWaitMs = 1000;
constexpr esp_err_t Waiting = -0x435350;
enum class Host { Dormant, Creating, Entered, Running, Cancelled, Exited };
std::atomic<Host> host{Host::Dormant};
std::atomic<esp_err_t> startup{ESP_ERR_INVALID_STATE};
std::atomic<esp_err_t> shutdownFailure{ESP_OK};
std::atomic<uint32_t> hostStackMinimum{0};
TaskFunction_t hostFunction = nullptr;

void revokePendingHost() {
  Host state = host.load();
  while (state == Host::Creating || state == Host::Entered) {
    if (host.compare_exchange_weak(state, Host::Cancelled)) return;
  }
}

void hostTrampoline(void *) {
  Host expected = Host::Creating;
  if (!host.compare_exchange_strong(expected, Host::Entered)) {
    // A creator that timed out before this task ran revoked the handoff.
    // A late task must never enter the SDK or use the retained endpoint state.
    host.store(Host::Exited);
    vTaskDelete(nullptr);
    return;
  }
  hostFunction(nullptr);
  // The pinned task normally self-deletes through freertos_deinit. Fail closed
  // if a future task body returns instead; do not claim a synchronized host.
  csp_ble_sdk_note_sync(ESP_FAIL);
  host.store(Host::Exited);
  vTaskDelete(nullptr);
}

bool waitExited(uint32_t milliseconds) {
  const TickType_t began = xTaskGetTickCount();
  const TickType_t limit = pdMS_TO_TICKS(milliseconds);
  while (host.load() != Host::Dormant && host.load() != Host::Exited) {
    if (TickType_t(xTaskGetTickCount() - began) >= limit) return false;
    vTaskDelay(1);
  }
  return true;
}
}

extern "C" esp_err_t csp_ble_sdk_create_host_task(TaskFunction_t function) {
  Host previous = host.load();
  if (!function || (previous != Host::Dormant && previous != Host::Exited))
    return ESP_ERR_INVALID_STATE;
  hostFunction = function;
  host.store(Host::Creating);
  TaskHandle_t created = nullptr;
  const BaseType_t result = xTaskCreatePinnedToCore(hostTrampoline, "nimble_host",
      NIMBLE_HS_STACK_SIZE, nullptr, configMAX_PRIORITIES - 4, &created, NIMBLE_CORE);
  if (result != pdPASS) {
    host.store(Host::Dormant);
    csp_ble_sdk_note_sync(ESP_ERR_NO_MEM);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

extern "C" esp_err_t csp_ble_sdk_host_task_exit(void) {
  // Only the owning host task invokes this hook. No external task is forcibly
  // deleted, and no shared SDK data is touched after publishing the exit ack.
  hostStackMinimum.store(uxTaskGetStackHighWaterMark(nullptr));
  host.store(Host::Exited);
  vTaskDelete(nullptr);
  return ESP_OK;
}

extern "C" void csp_ble_sdk_note_sync(esp_err_t result) {
  esp_err_t expected = Waiting;
  startup.compare_exchange_strong(expected, result);
  if (result != ESP_OK) {
    expected = ESP_OK;
    startup.compare_exchange_strong(expected, result);
  }
}
extern "C" bool csp_ble_sdk_start_allowed(void) { return startup.load() == Waiting || startup.load() == ESP_OK; }
extern "C" bool csp_ble_sdk_enter_host_loop(void) {
  if (!csp_ble_sdk_start_allowed()) return false;
  return csp_ble_sdk_commit_host_loop();
}
extern "C" bool csp_ble_sdk_commit_host_loop(void) {
  // This CAS arbitrates against revocation, even if the caller observed an
  // allowed startup just before the owner revoked a timed-out handoff.
  Host expected = Host::Entered;
  return host.compare_exchange_strong(expected, Host::Running);
}
extern "C" bool csp_ble_sdk_host_running(void) { return host.load() == Host::Running; }
extern "C" bool csp_ble_sdk_controller_idle(void) {
  return esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE;
}
extern "C" esp_err_t csp_ble_sdk_deinit_port(void) {
  if (shutdownFailure.load() != ESP_OK) return shutdownFailure.load();
  esp_err_t result = nimble_port_deinit();
  if (result == ESP_OK && !csp_ble_sdk_controller_idle()) result = ESP_ERR_INVALID_STATE;
  if (result != ESP_OK) shutdownFailure.store(result);
  return result;
}
extern "C" void csp_ble_sdk_note_init_failure(esp_err_t result) {
  // The pinned init attempts some internal cleanup. A nonidle controller after
  // failure is partial ownership, not permission to guess which stage to stop.
  if (result != ESP_OK && !csp_ble_sdk_controller_idle()) shutdownFailure.store(result);
}
extern "C" bool csp_ble_sdk_wait_host_exit(void) {
  revokePendingHost();
  return waitExited(ExitWaitMs);
}
extern "C" bool csp_ble_sdk_idle(void) {
  const Host state = host.load();
  return !csp_ble_sdk_transport_present() &&
      (state == Host::Dormant || state == Host::Exited) && csp_ble_sdk_controller_idle();
}
extern "C" bool csp_ble_sdk_ready(void) {
  return startup.load() == ESP_OK && csp_ble_sdk_host_running() &&
      esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
}
extern "C" uint32_t csp_ble_sdk_host_stack_min_bytes(void) { return hostStackMinimum.load(); }
extern "C" esp_err_t csp_ble_sdk_start(protocomm_t *pc, const protocomm_ble_config_t *config) {
  if (!csp_ble_sdk_idle() || !config || config->keep_ble_on) return ESP_ERR_INVALID_STATE;
  shutdownFailure.store(ESP_OK);
  hostStackMinimum.store(0);
  startup.store(Waiting);
  const esp_err_t result = protocomm_ble_start(pc, config);
  if (result != ESP_OK) {
    csp_ble_sdk_note_sync(result);
    return result;
  }
  const TickType_t began = xTaskGetTickCount(), limit = pdMS_TO_TICKS(StartupWaitMs);
  while (startup.load() == Waiting) {
    if (TickType_t(xTaskGetTickCount() - began) >= limit) {
      csp_ble_sdk_note_sync(ESP_ERR_TIMEOUT);
      revokePendingHost();
      break;
    }
    vTaskDelay(1);
  }
  const esp_err_t acknowledged = startup.load();
  return acknowledged == ESP_OK && !csp_ble_sdk_ready() ? ESP_ERR_INVALID_STATE : acknowledged;
}
extern "C" esp_err_t csp_ble_sdk_stop(protocomm_t *pc) {
  startup.store(ESP_ERR_INVALID_STATE); // prevents late sync from reviving ready
  // Revoke BOTH pre-loop states before transport decides whether host stop is
  // needed. A competing loop-commit either loses this CAS or is seen Running.
  revokePendingHost();
  if (shutdownFailure.load() != ESP_OK) return shutdownFailure.load();
  const esp_err_t result = protocomm_ble_stop(pc);
  if (result != ESP_OK) { shutdownFailure.store(result); return result; }
  if (!csp_ble_sdk_idle()) {
    shutdownFailure.store(ESP_ERR_INVALID_STATE); return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}
#endif
