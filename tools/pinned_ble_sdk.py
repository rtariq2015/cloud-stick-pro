"""Verify/reproduce the three minimal pinned SDK corrections (no SDK upgrade).

Run --write only to reproduce checked-in patched sources after review. Firmware
and desktop builds consume the same checked-in Security2 file.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / 'sdk/pinned_ble'

def blob(data):
    return hashlib.sha1(('blob '+str(len(data))+'\0').encode()+data).hexdigest()

def replace(source, before, after):
    if source.count(before) != 1:
        raise RuntimeError('Pinned SDK patch context mismatch: '+before[:100])
    return source.replace(before, after, 1)

def corrected_sources():
    source = (SDK/'upstream/protocomm_nimble.c').read_text()
    source = replace(source, '#include "protocomm_priv.h"', '#include "protocomm_priv.h"\n#include "ble_sdk_internal.h"\n\nextern esp_err_t esp_nimble_enable(void *host_task);')
    source = replace(source, '        ESP_LOGE(TAG, "Failed to init nimble %d ", rc);', '        ESP_LOGE(TAG, "Failed to init nimble %d ", rc);\n        csp_ble_sdk_note_init_failure(rc);')
    source = replace(source, '    nimble_port_run();', '    if (csp_ble_sdk_enter_host_loop()) nimble_port_run();')
    begin = source.index('static void\nsimple_ble_advertise(void)')
    end = source.index('\nstatic int\nsimple_ble_gap_event', begin)
    advertise = source[begin:end].replace('static void\n', 'static int\n', 1)
    advertise = advertise.replace('        return;', '        return ESP_ERR_INVALID_STATE;', 1)
    advertise = advertise.replace('        return;', '        return rc;')
    advertise = advertise.rstrip()[:-1] + '    return ESP_OK;\n}\n'
    source = source[:begin]+advertise+source[end:]
    source = replace(source, 'ESP_LOGE(TAG, "Resetting state; reason=%d", reason);', 'ESP_LOGE(TAG, "Resetting state; reason=%d", reason);\n    csp_ble_sdk_note_sync(ESP_FAIL);')
    begin = source.index('static void\nsimple_ble_on_sync(void)')
    end = source.index('\nvoid\nnimble_host_task', begin)
    sync = source[begin:end]
    sync = replace(sync, '    int rc;', '    int rc;\n    if (!csp_ble_sdk_start_allowed()) return;')
    sync = sync.replace('return;', 'csp_ble_sdk_note_sync(rc); return;')
    # The early denial does not use rc, which has not yet been assigned.
    sync = sync.replace('if (!csp_ble_sdk_start_allowed()) csp_ble_sdk_note_sync(rc); return;', 'if (!csp_ble_sdk_start_allowed()) return;')
    sync = replace(sync, '    simple_ble_advertise();', '    csp_ble_sdk_note_sync(simple_ble_advertise());')
    source = source[:begin]+sync+source[end:]
    source = replace(source, '    nimble_port_freertos_init(nimble_host_task);', '    rc = esp_nimble_enable(nimble_host_task);\n    if (rc != ESP_OK) goto err_deinit_port;')
    source = replace(source, '#if MYNEWT_VAL(BLE_GATTS)\nerr_deinit_port:\n    nimble_port_deinit();\n    return rc;\n#endif', 'err_deinit_port:;\n    esp_err_t deinit_result = nimble_port_deinit();\n    return deinit_result != ESP_OK ? deinit_result : rc;')
    source = replace(source, '''        ESP_LOGE(TAG, "simple_ble_start failed w/ error code 0x%x", err);
        free_gatt_ble_misc_memory(ble_config);''', '''        ESP_LOGE(TAG, "simple_ble_start failed w/ error code 0x%x", err);
        /* A failed init/deinit must not free ownership without quiescence. */
        if (!csp_ble_sdk_controller_idle() || !csp_ble_sdk_wait_host_exit()) return err;
        free_gatt_ble_misc_memory(ble_config);''')
    source = replace(source, '''        ret = nimble_port_stop();
        if (ret == 0) {
            nimble_port_deinit();
        } else {
            protoble_internal->pc_ble = pc;
            return ret;
        }''', '''        /* No stop event can be serviced by an unstarted/cancelled host task. */
        if (csp_ble_sdk_host_running()) {
            ret = nimble_port_stop();
            if (ret != ESP_OK) return ret;
        }
        if (!csp_ble_sdk_wait_host_exit()) return ESP_ERR_TIMEOUT;
        ret = nimble_port_deinit();
        if (ret != ESP_OK) return ret;
        if (!csp_ble_sdk_controller_idle()) return ESP_ERR_INVALID_STATE;''')
    source += '\n/* Owner-task query; lifecycle code never infers ownership from heap totals. */\nbool csp_ble_sdk_transport_present(void)\n{\n    return protoble_internal != NULL;\n}\n'
    # All deinitialization paths share one failure latch. Failed cleanup is
    # retained; calling stop after a failed startup cannot repeat deinit.
    source = source.replace('= nimble_port_deinit();', '= csp_ble_sdk_deinit_port();')

    task = (SDK/'upstream/nimble_port_freertos.c').read_text()
    task = replace(task, '#include <stddef.h>', '#include <stddef.h>\n#include "ble_sdk_internal.h"')
    task = replace(task, 'static TaskHandle_t host_task_h = NULL;\n', '')
    task = replace(task, '''    xTaskCreatePinnedToCore(host_task, "nimble_host", NIMBLE_HS_STACK_SIZE,
                            NULL, (configMAX_PRIORITIES - 4), &host_task_h, NIMBLE_CORE);
    return ESP_OK;''', '    return csp_ble_sdk_create_host_task((TaskFunction_t) host_task);')
    task = replace(task, '''    if (host_task_h) {
        vTaskDelete(host_task_h);
        host_task_h = NULL;
    }
    return ESP_OK;''', '    return csp_ble_sdk_host_task_exit();')

    security = (ROOT/'test/milestone1/sdk_vendor/security2.c').read_text()
    security = replace(security, '''    aes_gcm_iv_t *iv = (aes_gcm_iv_t *) iv_buf;
    iv->counter = htobe32(be32toh(iv->counter) + 1);''', '''    /* IV storage is byte-aligned in session_t. Do not cast it to a struct
     * requiring uint32_t alignment. Byte operations preserve network order. */
    uint32_t counter = ((uint32_t)iv_buf[8] << 24) | ((uint32_t)iv_buf[9] << 16) |
                       ((uint32_t)iv_buf[10] << 8) | iv_buf[11];
    ++counter;
    iv_buf[8] = (uint8_t)(counter >> 24);
    iv_buf[9] = (uint8_t)(counter >> 16);
    iv_buf[10] = (uint8_t)(counter >> 8);
    iv_buf[11] = (uint8_t)counter;''')
    security = replace(security, '''    aes_gcm_iv_t *iv = (aes_gcm_iv_t *) cur_session->iv;
    ret = mbedtls_ctr_drbg_random(&ctr_drbg, iv->session_id, SESSION_ID_LEN);''', '    ret = mbedtls_ctr_drbg_random(&ctr_drbg, cur_session->iv, SESSION_ID_LEN);')
    security = replace(security, '    iv->counter = htobe32(0x1);', '    cur_session->iv[8] = cur_session->iv[9] = cur_session->iv[10] = 0;\n    cur_session->iv[11] = 1;')
    before = '''    aes_gcm_iv_t *iv = (aes_gcm_iv_t *) cur_session->iv;
    if (be32toh(iv->counter) == 0) {'''
    after = '''    if ((cur_session->iv[8] | cur_session->iv[9] |
         cur_session->iv[10] | cur_session->iv[11]) == 0) {'''
    if security.count(before) != 2:
        raise RuntimeError('Pinned Security2 counter guard context mismatch')
    security = security.replace(before, after)
    # Target mbedTLS contexts can own heap-backed locks, even when the desktop
    # library does not. Release both temporary contexts on every initialized path.
    security = replace(security, '''        ESP_LOGE(TAG, "Failed to seed random number generator");
        free(device_proof);''', '''        ESP_LOGE(TAG, "Failed to seed random number generator");
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        free(device_proof);''')
    security = replace(security,
        '    ret = mbedtls_ctr_drbg_random(&ctr_drbg, cur_session->iv, SESSION_ID_LEN);',
        '''    ret = mbedtls_ctr_drbg_random(&ctr_drbg, cur_session->iv, SESSION_ID_LEN);
    /* The session owns the copied nonce; release temporary random state before
     * handling either success or random-generation failure. */
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);''')
    return {'protocomm_nimble.c':source, 'nimble_port_freertos.c':task, 'security2.c':security}

def verify(write=False):
    provenance = json.loads((SDK/'PROVENANCE.json').read_text())
    for name, entry in provenance['upstream'].items():
        path = ROOT/entry['path']
        if blob(path.read_bytes()) != entry['git_blob_sha']:
            raise RuntimeError('Unreviewed upstream SDK source: '+name)
    for name, source in corrected_sources().items():
        path = SDK/'patched'/name
        if write:
            path.write_text(source)
        elif not path.exists() or path.read_text() != source:
            raise RuntimeError('Patched SDK file differs from reviewed correction: '+name)
        original = (ROOT/provenance['upstream'][name]['path']).read_text()
        patch = ''.join(difflib.unified_diff(original.splitlines(True), source.splitlines(True), fromfile='upstream/'+name, tofile='patched/'+name))
        patchpath = SDK/(name+'.patch')
        if write:
            patchpath.write_text(patch)
        elif not patchpath.exists() or patchpath.read_text() != patch:
            raise RuntimeError('SDK review patch differs: '+name)
    private = (SDK/'upstream/protocomm_priv.h').read_bytes()
    if write:
        (SDK/'patched/protocomm_priv.h').write_bytes(private)
    elif (SDK/'patched/protocomm_priv.h').read_bytes() != private:
        raise RuntimeError('Pinned private transport header changed')
    return provenance

if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--write',action='store_true')
    verify(parser.parse_args().write)
    print('Pinned SDK source/provenance/patch verification passed')
