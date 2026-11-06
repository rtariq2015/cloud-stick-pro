"""Read-only source/build boundary checks; never opens a serial or block device."""
from pathlib import Path
import configparser,hashlib,json,re,subprocess
root=Path(__file__).resolve().parents[1]
base='468b66331db687fec26565d8373cd01cbb534ee1'
protected=['partitions.csv','boards/tdongles3.json','src/usb_storage.cpp','include/usb_storage.h',
 'include/pro_blockio.h','include/pro_snapshot.h','src/pro_core.cpp','include/pro_core.h',
 'src/pro_journal.cpp','include/pro_journal.h','src/sync_engine.cpp',
 'src/config_loader.cpp','include/config_loader.h','include/setup_access.h',
 'src/setup_identity.cpp','src/webdav_client.cpp']
rows=[]
for name in protected:
 old=subprocess.check_output(['git','-C',str(root),'show',base+':'+name])
 current=(root/name).read_bytes();assert old==current,name
 rows.append({'path':name,'sha256':hashlib.sha256(current).hexdigest(),'unchanged':True})
config=configparser.ConfigParser(interpolation=None);config.read(root/'platformio.ini')
assert config['platformio']['default_envs']=='tdongle_s3'
assert 'CSP_ENABLE_BLE_MANAGEMENT' not in config['env:tdongle_s3']['build_flags']
assert 'env:tdongle_s3_ble_m1' not in config
isolated=config['env:tdongle_s3_ble_m1_isolated']
assert '-DCSP_ENABLE_BLE_MANAGEMENT=1' in isolated['build_flags']
assert '-DCSP_BLE_SYNTHETIC_ONLY=1' in isolated['build_flags']
assert '-DCSP_BLE_ISOLATED_TEST=1' in isolated['build_flags']
for name in ['config_loader','diagnostics','guest_wifi','portal','pro_journal','setup_identity','setup_network','sync_engine','webdav_client','wifi_scan']:
 assert '-<'+name+'.cpp>' in isolated['build_src_filter'],name
cpp=(root/'src/ble_management.cpp').read_text()
for function in ['esp_restart','esp_bt_mem_release','esp_bt_controller_mem_release','nvs_set_blob','nvs_commit','save_config','reset_config','fopen','sdmmc_write_sectors']:
 assert not re.search(r'\b'+function+r'\s*\(',cpp),function
assert 'bounded=protocomm_security2' in cpp
assert 'pro::ble::encryptedFrameSize' in cpp
assert 'config.ble_bonding=false' in cpp
result={'passed':True,'baseline':base,'protected_files':rows,'default_ble_disabled':True,
 'ble_module_no_persistent_mutation_or_reset':True,'validation':'source boundaries, not physical USB/RF testing'}
out=root/'artifacts/milestone1-corrections';out.mkdir(parents=True,exist_ok=True)
(out/'safety-invariants.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='protected_files'}))
