#pragma once
#ifndef CSP_NIMBLE_STOP_REPAIR
#define CSP_NIMBLE_STOP_REPAIR 0
#endif
#ifndef CSP_PRODUCT_RC_PROFILE
#define CSP_PRODUCT_RC_PROFILE 0
#endif
#if CSP_PRODUCT_RC_PROFILE != 0 && CSP_PRODUCT_RC_PROFILE != 1
#error CSP_PRODUCT_RC_PROFILE must be zero or one
#endif
#if CSP_PRODUCT_RC_PROFILE && (!CSP_ENABLE_BACKUP_SETUP || CSP_BACKUP_FIXTURE_ONLY || !CSP_NIMBLE_STOP_REPAIR)
#error Release candidate requires the combined saved-folder runtime and NimBLE repair
#endif
#if CSP_PRODUCT_RC_PROFILE && (CSP_PRODUCT_LAB_PROFILE || CSP_PRODUCT_LAB2_PROFILE || CSP_PRODUCT_LAB3_PROFILE || CSP_PRODUCT_LAB4_PROFILE || CSP_PRODUCT_LAB5_PROFILE || CSP_PRODUCT_LAB6_PROFILE || CSP_PRODUCT_LAB7_PROFILE || CSP_UPLOAD_DIAGNOSTICS || CSP_PANIC_DIAGNOSTICS)
#error Release candidate cannot retain lab identities or diagnostic instrumentation
#endif
#if CSP_NIMBLE_STOP_REPAIR != 0 && CSP_NIMBLE_STOP_REPAIR != 1
#error CSP_NIMBLE_STOP_REPAIR must be zero or one
#endif
#if CSP_NIMBLE_STOP_REPAIR && (!CSP_ENABLE_BLE_MANAGEMENT || (!CSP_PRODUCT_LAB5_PROFILE && !CSP_PRODUCT_RC_PROFILE))
#error NimBLE stop repair requires the explicit BLE-enabled lab5 or release-candidate profile
#endif
