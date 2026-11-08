#!/usr/bin/env python3
"""Build an allowlisted application with an immutable, SHA-checked mem1 SDK.

No upload/reset/serial/cloud operation exists here. Linux Landlock denies build
subprocess writes outside this project, temporary files and PIO cache/state.
"""
import argparse
import ctypes
import difflib
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import selectors
import shlex
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
FROZEN=ROOT.parent/'cloud-stick-sdk-memory-20261104-asym-iram'
CORE=Path('/config/.unmanic/research/cloud-stick-sdk-memory-core-20261104-asym-iram')
BUILD_CORE=ROOT/'.pio/mem1-guarded-core'
PYTHON=Path('/config/.unmanic/research/cloud-stick-tools/bin/python3')
PIO=PYTHON.parent/'pio'
ENVIRONMENT='tdongle_s3_public_wifi_mem1'
PROFILES={
 'tdongle_s3_public_wifi_mem1':{
  'version':'0.3.3-public-wifi-mem1','output':'artifacts/public-wifi-memory-build',
  'flags':{'CSP_PUBLIC_WIFI_MEMORY_PROFILE':'1'},
 },
 'tdongle_s3_product_lab':{
  'version':'0.4.0-product-lab1','output':'artifacts/product-lab-build',
  'flags':{'CSP_PRODUCT_LAB_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1'},
 },
 'tdongle_s3_product_lab2':{
  'version':'0.4.0-product-lab2','output':'artifacts/product-lab2-build',
  'flags':{'CSP_PRODUCT_LAB_PROFILE':'1','CSP_PRODUCT_LAB2_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1'},
 },
 'tdongle_s3_product_lab3':{
  'version':'0.4.0-product-lab3','output':'artifacts/product-lab3-build',
  'flags':{'CSP_PRODUCT_LAB_PROFILE':'1','CSP_PRODUCT_LAB2_PROFILE':'1','CSP_PRODUCT_LAB3_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_UPLOAD_DIAGNOSTICS':'1'},
 },
 'tdongle_s3_product_lab4':{
  'version':'0.4.0-product-lab4','output':'artifacts/product-lab4-build',
  'flags':{'CSP_PRODUCT_LAB_PROFILE':'1','CSP_PRODUCT_LAB2_PROFILE':'1','CSP_PRODUCT_LAB3_PROFILE':'1','CSP_PRODUCT_LAB4_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_UPLOAD_DIAGNOSTICS':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1'},
 },
 'tdongle_s3_product_lab5':{
  'version':'0.4.0-product-lab5','output':'artifacts/product-lab5-build',
  'flags':{'CSP_PRODUCT_LAB_PROFILE':'1','CSP_PRODUCT_LAB2_PROFILE':'1','CSP_PRODUCT_LAB3_PROFILE':'1','CSP_PRODUCT_LAB4_PROFILE':'1','CSP_PRODUCT_LAB5_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_UPLOAD_DIAGNOSTICS':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_PANIC_DIAGNOSTICS':'1','CSP_NIMBLE_STOP_REPAIR':'1'},
 },
 'tdongle_s3_product_lab6':{
  'version':'0.4.0-product-lab6','output':'artifacts/product-lab6-build',
  'flags':{'CSP_PRODUCT_LAB_PROFILE':'1','CSP_PRODUCT_LAB2_PROFILE':'1','CSP_PRODUCT_LAB3_PROFILE':'1','CSP_PRODUCT_LAB4_PROFILE':'1','CSP_PRODUCT_LAB5_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_UPLOAD_DIAGNOSTICS':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_PANIC_DIAGNOSTICS':'1','CSP_NIMBLE_STOP_REPAIR':'1','CSP_PRODUCT_LAB6_PROFILE':'1'},
 },
 'tdongle_s3_product_lab7':{
  'version':'0.4.0-product-lab7','output':'artifacts/product-lab7-build',
  'flags':{'CSP_PRODUCT_LAB_PROFILE':'1','CSP_PRODUCT_LAB2_PROFILE':'1','CSP_PRODUCT_LAB3_PROFILE':'1','CSP_PRODUCT_LAB4_PROFILE':'1','CSP_PRODUCT_LAB5_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_UPLOAD_DIAGNOSTICS':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_PANIC_DIAGNOSTICS':'1','CSP_NIMBLE_STOP_REPAIR':'1','CSP_PRODUCT_LAB6_PROFILE':'1','CSP_PRODUCT_LAB7_PROFILE':'1'},
 },
 'tdongle_s3_product_rc1':{
  'version':'0.4.0-rc1','output':'artifacts/product-rc1-build',
  'flags':{'CSP_PRODUCT_RC_PROFILE':'1','CSP_BACKUP_FIXTURE_ONLY':'0','CSP_PUBLIC_WIFI_MEMORY_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_NIMBLE_STOP_REPAIR':'1'},
 },
 'tdongle_s3_product_dashboard1':{
  'version':'0.4.0-dashboard1','output':'artifacts/product-dashboard1-build',
  'flags':{'CSP_PRODUCT_RC_PROFILE':'1','CSP_BACKUP_FIXTURE_ONLY':'0','CSP_PUBLIC_WIFI_MEMORY_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_NIMBLE_STOP_REPAIR':'1','CSP_PRODUCT_DASHBOARD_TRIAL':'1','CSP_ENABLE_LOCAL_DASHBOARD':'1'},
 },
 'tdongle_s3_product_speed1':{
  'version':'0.4.0-speed1','output':'artifacts/product-speed1-build',
  'flags':{'CSP_PRODUCT_RC_PROFILE':'1','CSP_BACKUP_FIXTURE_ONLY':'0','CSP_PUBLIC_WIFI_MEMORY_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_NIMBLE_STOP_REPAIR':'1','CSP_PRODUCT_SPEED_TRIAL':'1','CSP_UPLOAD_THROUGHPUT_TRIAL':'1','CSP_UPLOAD_TX_BYTES':'4096'},
 },
 'tdongle_s3_product_speed2':{
  'version':'0.4.0-speed2','output':'artifacts/product-speed2-build',
  'flags':{'CSP_PRODUCT_RC_PROFILE':'1','CSP_BACKUP_FIXTURE_ONLY':'0','CSP_PUBLIC_WIFI_MEMORY_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_NIMBLE_STOP_REPAIR':'1','CSP_PRODUCT_SPEED_TRIAL':'1','CSP_UPLOAD_THROUGHPUT_TRIAL':'1','CSP_PRODUCT_SPEED2_PROFILE':'1','CSP_UPLOAD_TX_BYTES':'1460','CSP_BACKUP_WIFI_AWAKE':'1'},
 },
 'tdongle_s3_product_rc2':{
  'version':'0.4.0-rc2','output':'artifacts/product-rc2-build',
  'flags':{'CSP_PRODUCT_RC_PROFILE':'1','CSP_BACKUP_FIXTURE_ONLY':'0','CSP_PUBLIC_WIFI_MEMORY_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_NIMBLE_STOP_REPAIR':'1','CSP_PRODUCT_SPEED_TRIAL':'1','CSP_UPLOAD_THROUGHPUT_TRIAL':'1','CSP_PRODUCT_DASHBOARD_TRIAL':'1','CSP_ENABLE_LOCAL_DASHBOARD':'1','CSP_PRODUCT_RC2_PROFILE':'1','CSP_UPLOAD_TX_BYTES':'1460','CSP_BACKUP_WIFI_AWAKE':'1'},
 },
 'tdongle_s3_product_rc3':{
  'version':'0.4.0-rc3','output':'artifacts/product-rc3-build',
  'flags':{'CSP_PRODUCT_RC_PROFILE':'1','CSP_BACKUP_FIXTURE_ONLY':'0','CSP_PUBLIC_WIFI_MEMORY_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_NIMBLE_STOP_REPAIR':'1','CSP_PRODUCT_SPEED_TRIAL':'1','CSP_UPLOAD_THROUGHPUT_TRIAL':'1','CSP_PRODUCT_DASHBOARD_TRIAL':'1','CSP_ENABLE_LOCAL_DASHBOARD':'1','CSP_PRODUCT_RC3_PROFILE':'1','CSP_ENABLE_OWNER_CLOUD_SETUP':'1','CSP_ENABLE_OWNER_LOGIN':'1','CSP_ENABLE_SCAN_IDLE_GATE':'1','CSP_ENABLE_LCD_V2':'1','CSP_DASHBOARD_PASSIVE_CLOSE':'1','CSP_UPLOAD_TX_BYTES':'1460','CSP_BACKUP_WIFI_AWAKE':'1'},
 },
 'tdongle_s3_product_rc4':{
  'version':'0.4.0-rc4','output':'artifacts/product-rc4-build',
  'flags':{'CSP_PRODUCT_RC_PROFILE':'1','CSP_BACKUP_FIXTURE_ONLY':'0','CSP_PUBLIC_WIFI_MEMORY_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_NIMBLE_STOP_REPAIR':'1','CSP_PRODUCT_SPEED_TRIAL':'1','CSP_UPLOAD_THROUGHPUT_TRIAL':'1','CSP_PRODUCT_DASHBOARD_TRIAL':'1','CSP_ENABLE_LOCAL_DASHBOARD':'1','CSP_PRODUCT_RC4_PROFILE':'1','CSP_ENABLE_OWNER_CLOUD_SETUP':'1','CSP_ENABLE_OWNER_LOGIN':'1','CSP_ENABLE_SCAN_IDLE_GATE':'1','CSP_ENABLE_LCD_V2':'1','CSP_DASHBOARD_PASSIVE_CLOSE':'1','CSP_UPLOAD_TX_BYTES':'1460','CSP_BACKUP_WIFI_AWAKE':'1'},
 },
 'tdongle_s3_product_rc5':{
  'version':'0.4.0-rc5','output':'artifacts/product-rc5-build',
  'flags':{'CSP_PRODUCT_RC_PROFILE':'1','CSP_BACKUP_FIXTURE_ONLY':'0','CSP_PUBLIC_WIFI_MEMORY_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_NIMBLE_STOP_REPAIR':'1','CSP_PRODUCT_SPEED_TRIAL':'1','CSP_UPLOAD_THROUGHPUT_TRIAL':'1','CSP_PRODUCT_DASHBOARD_TRIAL':'1','CSP_ENABLE_LOCAL_DASHBOARD':'1','CSP_PRODUCT_RC5_PROFILE':'1','CSP_ENABLE_OWNER_CLOUD_SETUP':'1','CSP_ENABLE_OWNER_LOGIN':'1','CSP_ENABLE_SCAN_IDLE_GATE':'1','CSP_ENABLE_LCD_V2':'1','CSP_DASHBOARD_PASSIVE_CLOSE':'1','CSP_UPLOAD_TX_BYTES':'1460','CSP_BACKUP_WIFI_AWAKE':'1'},
 },
 'tdongle_s3_product_setup_dev':{
  'version':'0.4.1-setup-dev','output':'artifacts/product-setup-dev-build',
  'flags':{'CSP_PRODUCT_RC_PROFILE':'1','CSP_BACKUP_FIXTURE_ONLY':'0','CSP_PUBLIC_WIFI_MEMORY_PROFILE':'1','CSP_ENABLE_EVENT_LOG':'1','CSP_ENABLE_FILE_HISTORY':'1','CSP_ENABLE_REMOTE_HEALTH':'1','CSP_NIMBLE_STOP_REPAIR':'1','CSP_PRODUCT_SPEED_TRIAL':'1','CSP_UPLOAD_THROUGHPUT_TRIAL':'1','CSP_PRODUCT_DASHBOARD_TRIAL':'1','CSP_ENABLE_LOCAL_DASHBOARD':'1','CSP_PRODUCT_SETUP_DEV_PROFILE':'1','CSP_ENABLE_OWNER_CLOUD_SETUP':'1','CSP_ENABLE_OWNER_LOGIN':'1','CSP_ENABLE_SCAN_IDLE_GATE':'1','CSP_ENABLE_LCD_V2':'1','CSP_DASHBOARD_PASSIVE_CLOSE':'1','CSP_UPLOAD_TX_BYTES':'1460','CSP_BACKUP_WIFI_AWAKE':'1'},
 },

 'tdongle_s3_product_incremental_dev':{
  'version':'0.4.2-incremental-dev','output':'artifacts/product-incremental-dev-build',
  'flags':{'CSP_PRODUCT_INCREMENTAL_DEV_PROFILE':'1','CSP_ENABLE_FILE_INCREMENTAL_CACHE':'1'},
 },

}
# Keep the parent profile's explicit non-fixture and lifecycle requirements.
PROFILES['tdongle_s3_product_incremental_dev']['flags']={
    **PROFILES['tdongle_s3_product_setup_dev']['flags'],
    **PROFILES['tdongle_s3_product_incremental_dev']['flags'],
}
PROFILES['tdongle_s3_product_ui_dev']={
    'version':'0.4.3-ui-dev','output':'artifacts/product-ui-dev-build',
    'flags':{**PROFILES['tdongle_s3_product_incremental_dev']['flags'],
             'CSP_PRODUCT_UI_DEV_PROFILE':'1'},
}
PROFILES['tdongle_s3_product_wifi_dev']={
    'version':'0.4.4-wifi-dev','output':'artifacts/product-wifi-dev-build',
    'flags':{**PROFILES['tdongle_s3_product_ui_dev']['flags'],
             'CSP_PRODUCT_WIFI_DEV_PROFILE':'1','CSP_ENABLE_WIFI_FAIR_RETRY':'1'},
}
PROFILES['tdongle_s3_product_ble_dev']={
    'version':'0.4.5-ble-dev','output':'artifacts/product-ble-dev-build',
    'flags':{**PROFILES['tdongle_s3_product_wifi_dev']['flags'],
             'CSP_PRODUCT_BLE_DEV_PROFILE':'1','CSP_ENABLE_BLE_RELEASE_SETTLEMENT':'1'},
}
PROFILES['tdongle_s3_product_cache_dev']={
    'version':'0.4.6-cache-dev','output':'artifacts/product-cache-dev-build',
    'flags':{**PROFILES['tdongle_s3_product_ble_dev']['flags'],
             'CSP_PRODUCT_CACHE_DEV_PROFILE':'1','CSP_ENABLE_BLE_NVS_CACHE_RELEASE':'1'},
}
PROFILES['tdongle_s3_product_cache_lab']={
    'version':'0.4.6-cache-lab','output':'artifacts/product-cache-lab-build',
    'flags':{**PROFILES['tdongle_s3_product_cache_dev']['flags'],
             'CSP_PRODUCT_CACHE_LAB_PROFILE':'1','CSP_NVS_CACHE_LAB':'1'},
}
PROFILES['tdongle_s3_product_release_lab']={
    'version':'0.4.7-release-lab','output':'artifacts/product-release-lab-build',
    'flags':{**PROFILES['tdongle_s3_product_cache_dev']['flags'],
             'CSP_PRODUCT_RELEASE_LAB_PROFILE':'1','CSP_BLE_RELEASE_DIAGNOSTICS':'1'},
}
OUTPUT=ROOT/PROFILES[ENVIRONMENT]['output']
GUARD_SHA='076b5d5b50c34491c9550b97c03bebbc72035d6a2a5cf9c1decb0033803d5c13'
MANIFEST_SHA='1667e4e5650d161240a3ba6a271f02004717ddc1caec56cf178805f8e0738237'
DEFAULTS_SHA='5c4f5c7f54fa30327f57670f71c9a782b77230b768bc593651a3b2007c320c97'
HEADER_SHA='51f53e882e3e3b735e166ffa19b8a9580204c37c5ce7a256251538de76023976'
OPTIONS='\n'.join(('CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y','CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384','CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096','CONFIG_ESP_WIFI_IRAM_OPT=n','CONFIG_ESP_WIFI_RX_IRAM_OPT=n','CONFIG_TINYUSB_AUDIO_ENABLED=n','CONFIG_TINYUSB_NCM_ENABLED=n'))
BUILDER_HASHES={
 'platforms/espressif32/builder/frameworks/arduino.py':'798723c6277953a89f1fbab4fb9759405a40f10196f66eb18c8f4beefb8a949b',
 'platforms/espressif32/builder/frameworks/component_manager.py':'750747a65a3c794fd4e7ae67ff6be79457a69ca3ed4bf7f5ac58f7db16dead3c',
 'platforms/espressif32/builder/frameworks/espidf.py':'d71a1ec05d20454213c98384004c9588c450fa26160b08fa3cd2c7b8f0c6b280',
 'platforms/espressif32/platform.json':'35f4aef4bed0e739bc49dc55c7a43df4f4e4ffda463e1ad5ca3ef541369c3107',
 'platforms/espressif32/platform.py':'103aa64f5aaef2f2c1ca8dbaa42e53fcb5b987b76857aac2cc3cfeed9003fc89',
 'packages/framework-arduinoespressif32/package.json':'1b4d8e93a1fcd9e8935312d0d134bca889142a92b7415b660c77eb165bdde963',
}

class GuardError(RuntimeError): pass
def require(condition,message):
    if not condition:raise GuardError(message)
def profile_for(environment):
    require(environment in PROFILES,'Environment is not allowlisted: '+str(environment))
    return PROFILES[environment]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def identity(path):return {'bytes':path.stat().st_size,'sha256':sha(path)}

def verify_files(root,manifest):
    root=root.resolve()
    for relative,expected in manifest.items():
        name=PurePosixPath(relative)
        require(not name.is_absolute() and '..' not in name.parts and bool(name.parts),'Invalid manifest path')
        path=root/relative
        require(path.resolve().is_relative_to(root),'Manifest path escapes SDK')
        require(path.exists() and stat.S_ISREG(path.stat().st_mode),'Missing SDK file: '+relative)
        require(path.stat().st_size==expected['bytes'] and sha(path)==expected['sha256'],'SDK hash mismatch: '+relative)

def verify_sdk():
    guard_path=FROZEN/'SDK_REUSE_GUARD.json'
    require(sha(guard_path)==GUARD_SHA,'Frozen reuse-guard identity differs')
    guard=json.loads(guard_path.read_text())
    require(guard['private_platformio_core']==str(CORE),'Unexpected SDK core')
    manifest_path=FROZEN/guard['sdk_manifest']
    require(sha(manifest_path)==MANIFEST_SHA==guard['sdk_manifest_sha256'],'Frozen SDK manifest differs')
    manifest=json.loads(manifest_path.read_text());require(len(manifest)==6509,'Unexpected SDK file count')
    sdk=CORE/'packages/framework-arduinoespressif32-libs'
    verify_files(sdk,manifest)
    observed={str(p.relative_to(sdk)) for p in (sdk/'esp32s3').rglob('*') if p.is_file()}
    observed.update(('package.json','versions.txt','sdkconfig'))
    require(observed==set(manifest),'Unexpected/missing selected S3 SDK files')
    require(sha(sdk/'esp32s3/qio_qspi/include/sdkconfig.h')==HEADER_SHA,'SDK profile header differs')
    for relative,digest in BUILDER_HASHES.items():
        require(sha(CORE/relative)==digest,'Pinned builder/framework identity differs: '+relative)
    return {'checked_files':len(manifest),'manifest_sha256':MANIFEST_SHA,'profile_header_sha256':HEADER_SHA,'builder_hashes':BUILDER_HASHES}

def framework_files(directory):
    return {str(p.relative_to(directory)):identity(p) for p in directory.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc'}

def redirected_metadata_source():
    path=CORE/'platforms/espressif32/builder/frameworks/component_manager.py'
    text=path.read_text()
    before='backup_path = str(Path(self.config.arduino_libs_mcu) / f"pioarduino-build.py.{self.config.mcu}")'
    after='backup_path = str(Path(self.config.env.subst("$PROJECT_DIR")) / ".pio" / f"mem1-pioarduino-build.py.{self.config.mcu}")'
    require(text.count(before)==4,'Pinned metadata-backup contexts differ')
    text=text.replace(before,after)
    before='            shutil.copy2(backup_path, build_py_path)\n'
    after='            if Path(backup_path).read_bytes() != Path(build_py_path).read_bytes():\n                raise RuntimeError("Immutable SDK build script differs from its private backup")\n'
    require(text.count(before)==1,'Pinned metadata-restore context differs')
    return text.replace(before,after)

def verify_overlay():
    require(BUILD_CORE.is_dir() and not BUILD_CORE.is_symlink(),'Build overlay root invalid')
    packages=BUILD_CORE/'packages';original=CORE/'packages'
    for source in original.iterdir():
        target=packages/source.name
        if source.name=='framework-arduinoespressif32':
            require(target.is_dir() and not target.is_symlink(),'Mutable framework must be an independent copy')
            expected=framework_files(source);actual=framework_files(target)
            backup='tools/pioarduino-build.py.orig'
            if backup in actual:
                require(actual[backup]==expected['tools/pioarduino-build.py'],'Builder backup differs from pinned original')
                del actual[backup]
            require(actual==expected,'Framework copy changed beyond the one builder backup')
            for rel in expected:
                left,right=(source/rel).stat(),(target/rel).stat()
                require((left.st_dev,left.st_ino)!=(right.st_dev,right.st_ino),'Framework copy shares a file inode')
        else:
            require(target.is_symlink() and target.resolve()==source.resolve(),'Pinned tool/SDK package alias replaced: '+source.name)
    require({p.name for p in packages.iterdir()}=={p.name for p in original.iterdir()},'Unknown overlay package added')
    for name in ('penv','tools','dist','.cache'):
        path=BUILD_CORE/name
        require(path.is_symlink() and path.resolve()==(CORE/name).resolve(),'Overlay core alias differs: '+name)
    platform=BUILD_CORE/'platforms/espressif32'
    require(platform.is_dir() and not (BUILD_CORE/'platforms').is_symlink(),'Mutable platform metadata must be an independent copy')
    expected_platform=framework_files(CORE/'platforms/espressif32');actual_platform=framework_files(platform)
    relative='builder/frameworks/component_manager.py';expected_platform[relative]={'bytes':len(redirected_metadata_source().encode()),'sha256':hashlib.sha256(redirected_metadata_source().encode()).hexdigest()}
    require(actual_platform==expected_platform,'Platform metadata differs beyond reviewed backup redirection')
    sdk=packages/'framework-arduinoespressif32-libs'
    require(sdk.is_symlink() and sdk.resolve()==(original/'framework-arduinoespressif32-libs').resolve(),'Effective SDK alias replaced')
    return {'core':str(BUILD_CORE),'effective_sdk_root':str(sdk.resolve()),'sdk_alias_is_symlink':True,'mutable_framework_copy_files':len(expected),'mutable_framework_copy_bytes':sum(v['bytes'] for v in expected.values()),'only_permitted_framework_difference':'tools/pioarduino-build.py.orig if present, byte-identical pinned backup','platform_metadata_patch_sha256':expected_platform[relative]['sha256'],'platform_change':'Redirect four SDK backup paths into project .pio; identical-content restore is read-only comparison','no_shared_framework_file_inodes':True}

def prepare_overlay():
    if not BUILD_CORE.exists():
        require(not BUILD_CORE.is_symlink(),'Broken overlay root symlink')
        BUILD_CORE.mkdir(parents=True)
        packages=BUILD_CORE/'packages';packages.mkdir()
        for source in (CORE/'packages').iterdir():
            target=packages/source.name
            if source.name=='framework-arduinoespressif32':shutil.copytree(source,target,symlinks=False)
            else:target.symlink_to(source,target_is_directory=source.is_dir())
        for name in ('penv','tools','dist','.cache'):(BUILD_CORE/name).symlink_to(CORE/name,target_is_directory=True)
        for name in ('appstate.json','idf-env.json'):
            if (CORE/name).is_file():shutil.copy2(CORE/name,BUILD_CORE/name)
    platform_parent=BUILD_CORE/'platforms'
    if platform_parent.is_symlink():
        require(platform_parent.resolve()==(CORE/'platforms').resolve(),'Unknown platform alias; preserved')
        platform_parent.unlink()
    if not platform_parent.exists():
        platform_parent.mkdir()
        shutil.copytree(CORE/'platforms/espressif32',platform_parent/'espressif32',symlinks=False)
    path=platform_parent/'espressif32/builder/frameworks/component_manager.py'
    before=(CORE/'platforms/espressif32/builder/frameworks/component_manager.py').read_text();after=redirected_metadata_source()
    require(path.read_text() in (before,after),'Unknown existing platform metadata; preserved')
    if path.read_text()==before:
        path.write_text(after)
        (OUTPUT/'platform-backup-redirection.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='pinned/component_manager.py',tofile='private-build-metadata/component_manager.py')))
    return verify_overlay()

def defines(flags):
    tokens=shlex.split(flags) if isinstance(flags,str) else [token for line in flags for token in shlex.split(line)]
    result={}
    for token in tokens:
        if token.startswith('-D'):
            key,separator,value=token[2:].partition('=');result[key]=value if separator else '1'
    return result,tokens

def check_configuration(config,board,environment=None):
    environment=ENVIRONMENT if environment is None else environment
    profile=profile_for(environment)
    selected_environment=environment
    release_lab=environment=='tdongle_s3_product_release_lab'
    lab=environment=='tdongle_s3_product_cache_lab'
    cache=environment in ('tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab')
    settlement=environment in ('tdongle_s3_product_ble_dev','tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab')
    wifi=environment in ('tdongle_s3_product_wifi_dev','tdongle_s3_product_ble_dev','tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab')
    incremental=environment in ('tdongle_s3_product_incremental_dev','tdongle_s3_product_ui_dev','tdongle_s3_product_wifi_dev','tdongle_s3_product_ble_dev','tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab')
    section='env:'+environment
    # The new candidate inherits the reviewed setup profile. Validate all of its
    # old safety constraints, then require its two distinct identity/feature bits.
    # Other profiles must never silently acquire the cache.
    selected_values,_=defines(config.get(section,'build_flags'))
    for flag in ('CSP_PRODUCT_RELEASE_LAB_PROFILE','CSP_BLE_RELEASE_DIAGNOSTICS'):
        require(selected_values.get(flag,'0')==('1' if release_lab else '0'),'Release diagnostics require their separate identity: '+flag)
    for flag in ('CSP_PRODUCT_CACHE_LAB_PROFILE','CSP_NVS_CACHE_LAB'):
        require(selected_values.get(flag,'0')==('1' if lab else '0'),
                'Private cache fixture leaked across profiles: '+flag)
    for flag in ('CSP_PRODUCT_CACHE_DEV_PROFILE','CSP_ENABLE_BLE_NVS_CACHE_RELEASE'):
        require(selected_values.get(flag,'0')==('1' if cache else '0'),
                'NVS cache feature/identity leaked across profiles: '+flag)
    for flag in ('CSP_PRODUCT_BLE_DEV_PROFILE','CSP_ENABLE_BLE_RELEASE_SETTLEMENT'):
        require(selected_values.get(flag,'0')==('1' if settlement else '0'),
                'BLE settlement feature/identity leaked across profiles: '+flag)
    for flag in ('CSP_PRODUCT_WIFI_DEV_PROFILE','CSP_ENABLE_WIFI_FAIR_RETRY'):
        require(selected_values.get(flag,'0')==('1' if wifi else '0'),
                'Wi-Fi retry feature/identity leaked across profiles: '+flag)
    require(selected_values.get('CSP_PRODUCT_UI_DEV_PROFILE','0')==
            ('1' if environment in ('tdongle_s3_product_ui_dev','tdongle_s3_product_wifi_dev','tdongle_s3_product_ble_dev','tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab') else '0'),
            'UI identity leaked across profiles')
    for flag in ('CSP_PRODUCT_INCREMENTAL_DEV_PROFILE','CSP_ENABLE_FILE_INCREMENTAL_CACHE'):
        require(selected_values.get(flag,'0')==('1' if incremental else '0'),'Incremental feature/identity leaked across profiles: '+flag)
    if incremental:environment='tdongle_s3_product_setup_dev'
    require(config.get('platformio','default_envs')==['tdongle_s3'],'Default environment changed')
    require(config.get(section,'framework')==['arduino'],'Only Arduino app reuse is allowed')
    require(config.get(section,'board')=='tdongles3','Unexpected board')
    require(config.get(section,'platform')=='https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312/platform-espressif32.zip','Unexpected platform')
    for option in ('lib_ignore','custom_component_add','custom_component_remove','extra_scripts'):
        require(not config.get(section,option,default=None),'Mutable SDK/component operation not allowed: '+option)
    require(board==json.loads((FROZEN/'boards/tdongles3.json').read_text()),'Reviewed board definition changed')
    build=board['build'];extra=build.get('extra_flags',[])
    if not isinstance(extra,str):extra=' '.join(extra)
    memory=config.get(section,'board_build.memory_type',default=None) or build.get('arduino',{}).get('memory_type',build.get('memory_type',''))
    psram=config.get(section,'board_build.psram_type',default=None) or build.get('psram_type','')
    fingerprint=f"|{'PSRAM' in extra}|{memory}|{psram}"
    require(build['mcu']=='esp32s3' and fingerprint=='|False|qio_qspi|','Board memory fingerprint differs')
    entry=config.get(section,'custom_sdkconfig');require(entry.strip()==OPTIONS,'Exact seven SDK options differ')
    key=hashlib.md5((entry.strip()+build['mcu']+fingerprint).encode()).hexdigest()[:16]
    require(key=='52ba1142b3ef9bf7','SDK cache key differs')
    values,tokens=defines(config.get(section,'build_flags'))
    release_wraps={'-Wl,--wrap='+name for name in ('malloc','calloc','free','heap_caps_malloc','heap_caps_calloc','heap_caps_free')} if release_lab else set()
    require(values.get('CSP_PRODUCT_SETUP_DEV_PROFILE','0')==('1' if environment=='tdongle_s3_product_setup_dev' else '0'),'Setup development identity must remain separate from historical profiles')
    required={'CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE':'1','CSP_ENABLE_OWNED_GUEST_ROUTE':'1','CSP_ENABLE_BACKUP_SETUP':'1','CSP_ENABLE_BLE_MANAGEMENT':'1','CSP_ENABLE_WIFI_SETUP':'1','CSP_BACKUP_FIXTURE_ONLY':'1','CSP_USB_BENCH_AUTOMATION':'1','ARDUINO_LOOP_STACK_SIZE':'12288',**profile['flags']}
    require(all(values.get(k)==v for k,v in required.items()),'Required public/backup/stack feature flags differ')
    if environment in ('tdongle_s3_product_rc1','tdongle_s3_product_dashboard1','tdongle_s3_product_speed1','tdongle_s3_product_rc2','tdongle_s3_product_rc3','tdongle_s3_product_rc4','tdongle_s3_product_rc5','tdongle_s3_product_setup_dev','tdongle_s3_product_speed2'):
        require(all(values.get(k,'0')=='0' for k in (
            'CSP_PRODUCT_LAB_PROFILE','CSP_PRODUCT_LAB2_PROFILE','CSP_PRODUCT_LAB3_PROFILE',
            'CSP_PRODUCT_LAB4_PROFILE','CSP_PRODUCT_LAB5_PROFILE','CSP_PRODUCT_LAB6_PROFILE',
            'CSP_PRODUCT_LAB7_PROFILE','CSP_UPLOAD_DIAGNOSTICS','CSP_PANIC_DIAGNOSTICS',
            'CSP_BLE_ALLOC_TRACE','CSP_BLE_ISOLATED_TEST','CSP_BACKUP_LINK_PROBE')),
            'Release candidate cannot retain fixture/lab/trace identities')
        require({t for t in tokens if t.startswith('-Wl,--wrap=')}=={
            '-Wl,--wrap=esp_wifi_init','-Wl,--wrap=nvs_flash_erase',
            '-Wl,--wrap=nvs_flash_erase_partition'}|release_wraps,
            'Release candidate must retain exact NVS/Wi-Fi safety wrappers without diagnostics')
    else:
        require(values.get('CSP_PRODUCT_RC_PROFILE','0')=='0','Release candidate cannot reuse a lab identity')
        if environment not in ('tdongle_s3_product_lab2','tdongle_s3_product_lab3','tdongle_s3_product_lab4','tdongle_s3_product_lab5','tdongle_s3_product_lab6','tdongle_s3_product_lab7'):
            require(values.get('CSP_PRODUCT_LAB2_PROFILE','0')=='0','Clock-correction profile cannot use an earlier image identity')
        if environment not in ('tdongle_s3_product_lab3','tdongle_s3_product_lab4','tdongle_s3_product_lab5','tdongle_s3_product_lab6','tdongle_s3_product_lab7'):
            require(all(values.get(flag,'0')=='0' for flag in ('CSP_PRODUCT_LAB3_PROFILE','CSP_UPLOAD_DIAGNOSTICS')),'Diagnostic profile cannot use an earlier image identity')
        diagnostic_wraps={'-Wl,--wrap='+name for name in ('mbedtls_ssl_write','mbedtls_ssl_read','mbedtls_net_send')}
        if environment in ('tdongle_s3_product_lab3','tdongle_s3_product_lab4','tdongle_s3_product_lab5','tdongle_s3_product_lab6','tdongle_s3_product_lab7'):
            require(diagnostic_wraps.issubset(tokens),'Upload diagnostics require every reviewed transport wrapper')
        else:
            require(not diagnostic_wraps.intersection(tokens),'Upload wrappers cannot use an earlier image identity')
        if environment not in ('tdongle_s3_product_lab4','tdongle_s3_product_lab5','tdongle_s3_product_lab6','tdongle_s3_product_lab7'):
            require(all(values.get(flag,'0')=='0' for flag in ('CSP_PRODUCT_LAB4_PROFILE','CSP_ENABLE_FILE_HISTORY','CSP_ENABLE_REMOTE_HEALTH')),'History/health cannot use an earlier image identity')
        if environment not in ('tdongle_s3_product_lab5','tdongle_s3_product_lab6','tdongle_s3_product_lab7'):
            require(all(values.get(flag,'0')=='0' for flag in ('CSP_PRODUCT_LAB5_PROFILE','CSP_NIMBLE_STOP_REPAIR')),'NimBLE repair cannot use an earlier image identity')
        else:
            require('-Wl,--wrap=esp_panic_handler' in tokens,'Lab5 panic evidence wrapper missing')
        if environment not in ('tdongle_s3_product_lab6','tdongle_s3_product_lab7'):
            require(values.get('CSP_PRODUCT_LAB6_PROFILE','0')=='0','Route diagnostics cannot use an earlier image identity')
        if environment!='tdongle_s3_product_lab7':
            require(values.get('CSP_PRODUCT_LAB7_PROFILE','0')=='0','Startup restart repair cannot use an earlier image identity')
        if environment=='tdongle_s3_public_wifi_mem1':
            require(all(values.get(k,'0')=='0' for k in ('CSP_PRODUCT_LAB_PROFILE','CSP_ENABLE_EVENT_LOG')),'Product/event features cannot use the frozen public image identity')
    require('-Wl,--wrap=esp_wifi_init' in tokens,'Wi-Fi buffer policy wrap missing')
    if environment not in ('tdongle_s3_product_dashboard1','tdongle_s3_product_rc2','tdongle_s3_product_rc3','tdongle_s3_product_rc4','tdongle_s3_product_rc5','tdongle_s3_product_setup_dev'):
        require(all(values.get(k,'0')=='0' for k in ('CSP_PRODUCT_DASHBOARD_TRIAL','CSP_ENABLE_LOCAL_DASHBOARD')),
            'Dashboard code cannot reuse an earlier image identity')
    if environment not in ('tdongle_s3_product_speed1','tdongle_s3_product_rc2','tdongle_s3_product_rc3','tdongle_s3_product_rc4','tdongle_s3_product_rc5','tdongle_s3_product_setup_dev','tdongle_s3_product_speed2'):
        require(all(values.get(k,'0')=='0' for k in ('CSP_PRODUCT_SPEED_TRIAL','CSP_UPLOAD_THROUGHPUT_TRIAL')),
            'Upload trial cannot reuse an earlier image identity')
    if environment!='tdongle_s3_product_rc2':
        require(values.get('CSP_PRODUCT_RC2_PROFILE','0')=='0','Combined features cannot reuse an earlier image identity')
    if environment not in ('tdongle_s3_product_rc2','tdongle_s3_product_rc3','tdongle_s3_product_rc4','tdongle_s3_product_rc5','tdongle_s3_product_setup_dev','tdongle_s3_product_speed2'):
        require(values.get('CSP_BACKUP_WIFI_AWAKE','0')=='0','Awake-radio policy cannot reuse an earlier image identity')
    if environment!='tdongle_s3_product_speed2':
        require(values.get('CSP_PRODUCT_SPEED2_PROFILE','0')=='0','Original-size stream comparison requires its distinct image identity')
    if environment!='tdongle_s3_product_rc3':
        require(values.get('CSP_PRODUCT_RC3_PROFILE','0')=='0','RC3 identity cannot be reused by another profile')
    if environment!='tdongle_s3_product_rc4':
        require(values.get('CSP_PRODUCT_RC4_PROFILE','0')=='0','RC4 identity cannot be reused by another profile')
    if environment!='tdongle_s3_product_rc5':
        require(values.get('CSP_PRODUCT_RC5_PROFILE','0')=='0','RC5 identity cannot be reused by another profile')
    if environment not in ('tdongle_s3_product_rc3','tdongle_s3_product_rc4','tdongle_s3_product_rc5','tdongle_s3_product_setup_dev'):
        require(all(values.get(k,'0')=='0' for k in ('CSP_ENABLE_OWNER_CLOUD_SETUP','CSP_ENABLE_OWNER_LOGIN','CSP_ENABLE_SCAN_IDLE_GATE','CSP_ENABLE_LCD_V2','CSP_DASHBOARD_PASSIVE_CLOSE')),
            'Integrated features require their distinct RC3, RC4 or RC5 identity')
    for name in ('tdongle_s3','tdongle_s3_public_wifi_lab'):
        old,_=defines(config.get('env:'+name,'build_flags'))
        require('CSP_PUBLIC_WIFI_MEMORY_PROFILE' not in old,'Memory profile leaked into existing environment')
        require(not config.has_option('env:'+name,'custom_sdkconfig'),'Custom SDK leaked into existing environment')
    default,_=defines(config.get('env:tdongle_s3','build_flags'))
    require('CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE' not in default,'Public Wi-Fi leaked into default environment')
    return {'environment':selected_environment,'version':profile['version'],'framework':'arduino','fingerprint':fingerprint,'cache_key':key,'options':entry.strip(),'required_flags':required,'default_and_stock_public_unchanged':True}

def prepare_defaults(destination,source,digest,write):
    require(sha(source)==digest,'Approved source defaults differ')
    if destination.exists():
        require(destination.is_file() and sha(destination)==digest,'Unknown existing sdkconfig.defaults; preserved without overwrite')
        return 'existing identical'
    if not write:return 'absent; exact approved copy required before build'
    descriptor=os.open(destination,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(descriptor,'wb') as handle:handle.write(source.read_bytes());handle.flush();os.fsync(handle.fileno())
    require(sha(destination)==digest,'Copied defaults verification failed')
    return 'copied exact approved defaults into public stage'

class Landlock:
    # Restrict writes only, leaving normal compiler/library reads and execution.
    WRITE=1<<1;TRUNCATE=1<<14
    ACCESS=WRITE|sum(1<<n for n in range(4,15))
    class Ruleset(ctypes.Structure):_fields_=[('handled_access_fs',ctypes.c_uint64)]
    class PathRule(ctypes.Structure):
        _pack_=1
        _fields_=[('allowed_access',ctypes.c_uint64),('parent_fd',ctypes.c_int32)]
    @staticmethod
    def abi():
        libc=ctypes.CDLL(None,use_errno=True);libc.syscall.restype=ctypes.c_long
        return libc.syscall(444,ctypes.c_void_p(),0,1)
    @classmethod
    def restrict(cls,directories,files=()):
        libc=ctypes.CDLL(None,use_errno=True);libc.syscall.restype=ctypes.c_long
        require(cls.abi()>=3,'Landlock ABI3+ required; no unprotected build fallback')
        attr=cls.Ruleset(cls.ACCESS);ruleset=libc.syscall(444,ctypes.byref(attr),ctypes.sizeof(attr),0)
        if ruleset<0:raise OSError(ctypes.get_errno(),'Landlock ruleset')
        try:
            for path,access in [(p,cls.ACCESS) for p in directories]+[(p,cls.WRITE|cls.TRUNCATE) for p in files]:
                fd=os.open(path,os.O_PATH|os.O_CLOEXEC)
                try:
                    rule=cls.PathRule(access,fd)
                    if libc.syscall(445,ruleset,1,ctypes.byref(rule),0)<0:raise OSError(ctypes.get_errno(),'Landlock path rule')
                finally:os.close(fd)
            if libc.prctl(38,1,0,0,0)!=0:raise OSError(ctypes.get_errno(),'no_new_privs')
            if libc.syscall(446,ruleset,0)<0:raise OSError(ctypes.get_errno(),'Landlock restrict_self')
        finally:os.close(ruleset)

def source_snapshot():
    paths=[ROOT/'platformio.ini',ROOT/'partitions.csv',ROOT/'sdkconfig.defaults']
    paths.extend(p for folder in ('src','include','lib','phone','sdk','boards','tools') for p in (ROOT/folder).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix!='.pyc')
    return {str(p.relative_to(ROOT)):identity(p) for p in paths if p.exists()}

def child_environment():
    forbidden=('IDF_PATH','IDF_TARGET','SDKCONFIG','SDKCONFIG_DEFAULTS','CFLAGS','CXXFLAGS','CPPFLAGS','LDFLAGS')
    require(not any(os.environ.get(k) for k in forbidden),'Ambient SDK/compiler override present; stop before PIO')
    env={k:v for k,v in os.environ.items() if not k.startswith('PLATFORMIO_')}
    env.update(PLATFORMIO_CACHE_DIR=str(ROOT/'.pio/pio-cache'),PLATFORMIO_CORE_DIR=str(BUILD_CORE),PLATFORMIO_RUN_JOBS='2',PLATFORMIO_SETTING_ENABLE_TELEMETRY='false',CMAKE_BUILD_PARALLEL_LEVEL='2',CSP_BUILD_PIO_PATH=str(PIO),PYTHONDONTWRITEBYTECODE='1',PYTHONUNBUFFERED='1',PYTHONNOUSERSITE='1')
    return env

def run_build(environment):
    require(Landlock.abi()>=3,'Landlock unavailable; build refused')
    require(not ROOT.resolve().is_relative_to(FROZEN.resolve()) and not FROZEN.resolve().is_relative_to(ROOT.resolve()),'Stage/frozen roots overlap')
    command=[str(PIO),'run','-e',ENVIRONMENT,'-j2']
    directories=[ROOT,Path('/tmp'),CORE/'.cache']
    # Compiler/dependency-query subprocesses use DEVNULL for harmless redirection.
    files=[Path('/dev/null')]
    log=OUTPUT/'build.log';deadline=time.monotonic()+1200
    forbidden=(b'reinstall arduino framework',b'add "custom_sdkconfig" settings',b'copied compiled',b'reading cmake configuration',b'starting arduino compile')
    process=subprocess.Popen(command,cwd=ROOT,env=environment,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True,preexec_fn=lambda:Landlock.restrict(directories,files))
    tail=b''
    try:
        with log.open('wb') as handle,selectors.DefaultSelector() as selector:
            selector.register(process.stdout,selectors.EVENT_READ)
            while selector.get_map():
                require(time.monotonic()<deadline,'Build timeout')
                for key,_ in selector.select(timeout=2):
                    data=os.read(key.fileobj.fileno(),65536)
                    if not data:selector.unregister(key.fileobj);continue
                    handle.write(data);handle.flush();sys.stdout.buffer.write(data);sys.stdout.buffer.flush()
                    text=(tail+data).lower();require(not any(word in text for word in forbidden),'Unexpected SDK rebuild/reinstall attempt; write sandbox retained')
                    tail=text[-1024:]
            code=process.wait(timeout=10);require(code==0,'App build failed with exit '+str(code))
    finally:
        if process.poll() is None:
            os.killpg(process.pid,signal.SIGTERM)
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
    return {'command':command,'log':identity(log),'landlock_write_allowlist':[str(p) for p in directories+files],'sdk_rebuild_seen':False}

HEALTH_STORAGE_OBSERVER='''#if defined(CSP_ENABLE_REMOTE_HEALTH) && CSP_ENABLE_REMOTE_HEALTH
bool storage_read_view_active(){
  if(!mutex||xSemaphoreTake(mutex,0)!=pdTRUE)return true;
  const bool active=readMounted||driveRegistered;xSemaphoreGive(mutex);return active;
}
#endif
'''

def verify_storage_source(current,baseline,allow_observer=False):
    if current==baseline:return False
    anchor='bool storage_media_detected() {'
    require(allow_observer and baseline.count(anchor)==1,'USB storage callbacks changed')
    require(current==baseline.replace(anchor,HEALTH_STORAGE_OBSERVER+anchor),'Only the exact reviewed read-only observer may differ')
    return True

def verify_incremental_storage_sources():
    """Pin the reviewed callback instrumentation without weakening old gates."""
    path=ROOT/'docs/releases/0.4.2-source-review.json'
    require(path.is_file(),'Incremental source review has not been frozen')
    review=json.loads(path.read_text())
    require(review.get('schema')=='cloud-stick-incremental-source-review/v1','Wrong incremental review schema')
    expected=review.get('sources',{})
    require({'src/usb_storage.cpp','include/usb_storage.h','src/pro_core.cpp',
             'include/pro_core.h','src/sync_engine.cpp'}.issubset(expected),
            'Incremental review omits a storage/core boundary')
    verify_files(ROOT,expected)
    return {'review_sha256':sha(path),'checked_sources':len(expected),
            'storage_safety_review':'read-only instrumentation; sole host writer unchanged'}

def inspect_image():
    build=ROOT/'.pio/build'/ENVIRONMENT;binary=build/'firmware.bin'
    version=profile_for(ENVIRONMENT)['version']
    require(version.encode() in binary.read_bytes(),'Candidate version absent')
    if ENVIRONMENT!='tdongle_s3_product_cache_lab':
        for text in (b'BENCH_NVS_CACHE',b'BENCH_WIFI_DROP',b'csp-cache-lab'):
            require(text not in binary.read_bytes(),'Private cache fixture leaked into non-lab image')
    for marker in (b'RELEASE_TRACE',b'RELEASE_PAGE '):
        require((marker in binary.read_bytes())==(ENVIRONMENT=='tdongle_s3_product_release_lab'),
                'Release diagnostic strings/identity differ')
    require((build/'partitions.bin').read_bytes()==(FROZEN/'artifacts/memory-profile-mem1/partitions.bin').read_bytes(),'Partition layout differs')
    incremental_review=None
    if ENVIRONMENT in ('tdongle_s3_product_incremental_dev','tdongle_s3_product_ui_dev','tdongle_s3_product_wifi_dev','tdongle_s3_product_ble_dev','tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab'):
        incremental_review=verify_incremental_storage_sources()
        observer=True
    else:
        observer=verify_storage_source((ROOT/'src/usb_storage.cpp').read_text(),(FROZEN/'src/usb_storage.cpp').read_text(),ENVIRONMENT in ('tdongle_s3_product_lab4','tdongle_s3_product_lab5','tdongle_s3_product_lab6','tdongle_s3_product_lab7','tdongle_s3_product_rc1','tdongle_s3_product_dashboard1','tdongle_s3_product_speed1','tdongle_s3_product_rc2','tdongle_s3_product_rc3','tdongle_s3_product_rc4','tdongle_s3_product_rc5','tdongle_s3_product_setup_dev','tdongle_s3_product_speed2'))
    tool=CORE/'packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm'
    nm=subprocess.run([str(tool),'-S','--defined-only',str(build/'firmware.elf')],capture_output=True,text=True,check=True).stdout
    symbols={parts[3]:int(parts[1],16) for line in nm.splitlines() if len(parts:=line.split())==4}
    for name in ('ncm_epbuf','ep_in_sw_buf','ep_out_sw_buf','audiod_open','netd_open'):require(name not in symbols,'Unused USB class returned: '+name)
    for name,size in {'_mscd_epbuf':4096,'_transfer_buf':4096,'_hidh_epbuf':2688}.items():require(symbols.get(name)==size,'Used USB buffer differs: '+name)
    for name in ('mscd_open','cdcd_open','tud_msc_read10_cb','tud_msc_write10_cb','tud_cdc_line_state_cb','tud_descriptor_configuration_cb','tud_descriptor_device_cb'):require(name in symbols,'Used USB path absent: '+name)
    mapping=(build/'firmware.map').read_text();require('libespressif__esp-dl.a(' not in mapping and 'libfbs_model.a(' not in mapping,'Unreviewed ESP-DL code linked')
    result={'version':version,'files':{name:identity(build/name) for name in ('firmware.bin','firmware.elf','firmware.map','partitions.bin')},'used_usb_buffers_unchanged':True,'audio_ncm_absent':True,'partition_bytes_equal':True,'usb_callbacks_source_equal':True,'usb_storage_full_source_equal':not observer,'exact_readonly_health_observer_added':observer}
    if incremental_review is not None:
        result.update(usb_callbacks_source_equal=False,
                      exact_readonly_health_observer_added=False,
                      incremental_storage_review=incremental_review)
    out=OUTPUT/'app';out.mkdir(exist_ok=True)
    for name in result['files']:(out/name).write_bytes((build/name).read_bytes())
    return result

def main():
    global ENVIRONMENT,OUTPUT
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check-only',action='store_true');parser.add_argument('--self-test',action='store_true');parser.add_argument('--environment',choices=tuple(PROFILES),default='tdongle_s3_public_wifi_mem1');args=parser.parse_args()
    ENVIRONMENT=args.environment;OUTPUT=ROOT/profile_for(ENVIRONMENT)['output']
    if args.self_test:return self_test()
    try:from platformio.project.config import ProjectConfig
    except ModuleNotFoundError:
        # A venv executable and /usr/bin/python3 can resolve to the same binary
        # while their module search paths differ. Compare environment prefixes.
        require(Path(sys.prefix).resolve()!=PYTHON.parent.parent.resolve(),'Pinned PlatformIO Python unavailable')
        os.execv(PYTHON,[str(PYTHON),str(Path(__file__).resolve()),*sys.argv[1:]])
    OUTPUT.mkdir(parents=True,exist_ok=True)
    result={'environment':ENVIRONMENT,'scope':'Source/app build only; no hardware/cloud/site operation','base_commit':subprocess.run(['git','rev-parse','HEAD'],cwd=ROOT,capture_output=True,text=True,check=True).stdout.strip()}
    failure=None
    try:
        result['sdk_before']=verify_sdk()
        result['configuration']=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),json.loads((ROOT/'boards/tdongles3.json').read_text()))
        if ENVIRONMENT in ('tdongle_s3_product_lab5','tdongle_s3_product_lab6','tdongle_s3_product_lab7','tdongle_s3_product_rc1','tdongle_s3_product_dashboard1','tdongle_s3_product_speed1','tdongle_s3_product_rc2','tdongle_s3_product_rc3','tdongle_s3_product_rc4','tdongle_s3_product_rc5','tdongle_s3_product_setup_dev','tdongle_s3_product_incremental_dev','tdongle_s3_product_ui_dev','tdongle_s3_product_wifi_dev','tdongle_s3_product_ble_dev','tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab','tdongle_s3_product_speed2'):
            pinned=subprocess.run([str(PYTHON),'tools/pinned_nimble_stop.py'],cwd=ROOT,capture_output=True,text=True)
            require(pinned.returncode==0 and not pinned.stderr,'Project-side NimBLE pinned backport differs')
            result['nimble_stop_backport_verified']=True
        require((FROZEN/'sdkconfig.defaults').read_text().splitlines()[0]=='# TASMOTA__52ba1142b3ef9bf7','Frozen marker differs')
        result['defaults']=prepare_defaults(ROOT/'sdkconfig.defaults',FROZEN/'sdkconfig.defaults',DEFAULTS_SHA,not args.check_only)
        result['source_before']=source_snapshot()
        require(Landlock.abi()>=3,'Required kernel write restriction unavailable')
        if not args.check_only:
            result['overlay_before']=prepare_overlay()
            result['build']=run_build(child_environment())
            result['overlay_after']=verify_overlay();result['sdk_before_image_inspection']=verify_sdk()
            result['image']=inspect_image()
        result['source_after']=source_snapshot();require(result['source_before']==result['source_after'],'Source changed during app build')
        result['passed']=True;result['check_only']=args.check_only
    except Exception as error:
        result['passed']=False;result['error']=str(error);failure=error
    finally:
        try:result['sdk_after']=verify_sdk()
        except Exception as error:result['passed']=False;result['sdk_after_error']=str(error);failure=error
        if BUILD_CORE.exists():
            try:result['overlay_final']=verify_overlay()
            except Exception as error:result['passed']=False;result['overlay_after_error']=str(error);failure=error
        result['tool']=identity(Path(__file__))
        (OUTPUT/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    if failure:print('GUARD/BUILD FAILED:',failure,file=sys.stderr);return 1
    print('PASS: '+ENVIRONMENT+' exact frozen SDK verified before/after; '+('check only' if args.check_only else 'app-only build complete'))
    return 0

def self_test():
    class Tests(unittest.TestCase):
        def setUp(self):self.temp=tempfile.TemporaryDirectory(prefix='csp-sdk-guard-');self.root=Path(self.temp.name)
        def tearDown(self):self.temp.cleanup()
        def test_incremental_profile_identity_and_default_off(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            config=ProjectConfig(str(ROOT/'platformio.ini'))
            result=check_configuration(config,board,'tdongle_s3_product_incremental_dev')
            self.assertEqual(result['version'],'0.4.2-incremental-dev')
            self.assertEqual(result['required_flags']['CSP_BACKUP_FIXTURE_ONLY'],'0')
            for flag in ('CSP_ENABLE_FILE_INCREMENTAL_CACHE','CSP_PRODUCT_INCREMENTAL_DEV_PROFILE'):
                for target,value in (('tdongle_s3_product_incremental_dev','0'),('tdongle_s3_product_setup_dev','1')):
                    candidate=ProjectConfig(str(ROOT/'platformio.ini'));original=candidate.get
                    candidate.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'='+value] if section=='env:'+target and option=='build_flags' else original(section,option,**kw)
                    self.assertRaises(GuardError,check_configuration,candidate,board,target)
            default,_=defines(config.get('env:tdongle_s3','build_flags'))
            self.assertNotIn('CSP_ENABLE_FILE_INCREMENTAL_CACHE',default)
        def test_ui_profile_preserves_incremental_flags_and_rejects_identity_leaks(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            name='tdongle_s3_product_ui_dev'
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),board,name)
            self.assertEqual(result['version'],'0.4.3-ui-dev')
            self.assertEqual({k:v for k,v in PROFILES[name]['flags'].items()
                              if k!='CSP_PRODUCT_UI_DEV_PROFILE'},
                             PROFILES['tdongle_s3_product_incremental_dev']['flags'])
            self.assertNotEqual(PROFILES[name]['output'],PROFILES['tdongle_s3_product_incremental_dev']['output'])
            for profile in PROFILES:
                candidate=ProjectConfig(str(ROOT/'platformio.ini'));original=candidate.get
                value='0' if profile in (name,'tdongle_s3_product_wifi_dev','tdongle_s3_product_ble_dev','tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab') else '1'
                candidate.get=lambda section,option,**kw:original(section,option,**kw)+['-DCSP_PRODUCT_UI_DEV_PROFILE='+value] if section=='env:'+profile and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,candidate,board,profile)
        def test_wifi_retry_profile_preserves_ui_and_rejects_identity_leaks(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            name='tdongle_s3_product_wifi_dev'
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),board,name)
            self.assertEqual(result['version'],'0.4.4-wifi-dev')
            self.assertEqual({k:v for k,v in PROFILES[name]['flags'].items()
                              if k not in ('CSP_PRODUCT_WIFI_DEV_PROFILE','CSP_ENABLE_WIFI_FAIR_RETRY')},
                             PROFILES['tdongle_s3_product_ui_dev']['flags'])
            self.assertNotEqual(PROFILES[name]['output'],PROFILES['tdongle_s3_product_ui_dev']['output'])
            text=(ROOT/'include/pro_device.h').read_text()
            start=text.index('#if ',text.index('#include "ble_feature.h"'))
            block=text[start:text.index('\n#endif',start)+len('\n#endif')]
            for profile in (name,'tdongle_s3_product_ui_dev'):
                flags=['-D'+key+'='+value for key,value in PROFILES[profile]['flags'].items()]
                output=subprocess.run([shutil.which('g++'),'-E','-P','-x','c++',*flags,'-'],
                    input=block,capture_output=True,text=True,check=True).stdout
                self.assertIn('FirmwareVersion="'+PROFILES[profile]['version']+'"',output)
            for flag in ('CSP_PRODUCT_WIFI_DEV_PROFILE','CSP_ENABLE_WIFI_FAIR_RETRY'):
                for profile in PROFILES:
                    candidate=ProjectConfig(str(ROOT/'platformio.ini'));original=candidate.get
                    value='0' if profile in (name,'tdongle_s3_product_ble_dev','tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab') else '1'
                    candidate.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'='+value] if section=='env:'+profile and option=='build_flags' else original(section,option,**kw)
                    self.assertRaises(GuardError,check_configuration,candidate,board,profile)
        def test_ble_settlement_profile_preserves_wifi_and_rejects_leaks(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            name='tdongle_s3_product_ble_dev'
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),board,name)
            self.assertEqual(result['version'],'0.4.5-ble-dev')
            self.assertEqual({k:v for k,v in PROFILES[name]['flags'].items()
                              if k not in ('CSP_PRODUCT_BLE_DEV_PROFILE','CSP_ENABLE_BLE_RELEASE_SETTLEMENT')},
                             PROFILES['tdongle_s3_product_wifi_dev']['flags'])
            self.assertNotEqual(PROFILES[name]['output'],PROFILES['tdongle_s3_product_wifi_dev']['output'])
            text=(ROOT/'include/pro_device.h').read_text()
            start=text.index('#if ',text.index('#include "ble_feature.h"'))
            block=text[start:text.index('\n#endif',start)+len('\n#endif')]
            for profile in (name,'tdongle_s3_product_wifi_dev'):
                flags=['-D'+key+'='+value for key,value in PROFILES[profile]['flags'].items()]
                output=subprocess.run([shutil.which('g++'),'-E','-P','-x','c++',*flags,'-'],
                    input=block,capture_output=True,text=True,check=True).stdout
                self.assertIn('FirmwareVersion="'+PROFILES[profile]['version']+'"',output)
            for flag in ('CSP_PRODUCT_BLE_DEV_PROFILE','CSP_ENABLE_BLE_RELEASE_SETTLEMENT'):
                for profile in PROFILES:
                    candidate=ProjectConfig(str(ROOT/'platformio.ini'));original=candidate.get
                    value='0' if profile in (name,'tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab') else '1'
                    candidate.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'='+value] if section=='env:'+profile and option=='build_flags' else original(section,option,**kw)
                    self.assertRaises(GuardError,check_configuration,candidate,board,profile)
        def test_nvs_cache_profile_preserves_ble_and_rejects_leaks(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            name='tdongle_s3_product_cache_dev'
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),board,name)
            self.assertEqual(result['version'],'0.4.6-cache-dev')
            self.assertEqual({k:v for k,v in PROFILES[name]['flags'].items()
                              if k not in ('CSP_PRODUCT_CACHE_DEV_PROFILE','CSP_ENABLE_BLE_NVS_CACHE_RELEASE')},
                             PROFILES['tdongle_s3_product_ble_dev']['flags'])
            self.assertNotEqual(PROFILES[name]['output'],PROFILES['tdongle_s3_product_ble_dev']['output'])
            for flag in ('CSP_PRODUCT_CACHE_DEV_PROFILE','CSP_ENABLE_BLE_NVS_CACHE_RELEASE'):
                for profile in PROFILES:
                    candidate=ProjectConfig(str(ROOT/'platformio.ini'));original=candidate.get
                    value='0' if profile in (name,'tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab') else '1'
                    candidate.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'='+value] if section=='env:'+profile and option=='build_flags' else original(section,option,**kw)
                    self.assertRaises(GuardError,check_configuration,candidate,board,profile)
        def test_private_cache_lab_inherits_safety_and_never_leaks(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            name='tdongle_s3_product_cache_lab'
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),board,name)
            self.assertEqual(result['version'],'0.4.6-cache-lab')
            self.assertEqual({k:v for k,v in PROFILES[name]['flags'].items()
                              if k not in ('CSP_PRODUCT_CACHE_LAB_PROFILE','CSP_NVS_CACHE_LAB')},
                             PROFILES['tdongle_s3_product_cache_dev']['flags'])
            for flag in ('CSP_PRODUCT_CACHE_LAB_PROFILE','CSP_NVS_CACHE_LAB'):
                for profile in PROFILES:
                    candidate=ProjectConfig(str(ROOT/'platformio.ini'));original=candidate.get
                    value='0' if profile==name else '1'
                    candidate.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'='+value] if section=='env:'+profile and option=='build_flags' else original(section,option,**kw)
                    self.assertRaises(GuardError,check_configuration,candidate,board,profile)
        def test_speed2_exact_size_power_and_identity(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),board,'tdongle_s3_product_speed2')
            self.assertEqual(result['version'],'0.4.0-speed2')
            for define in ('CSP_UPLOAD_TX_BYTES=4096','CSP_BACKUP_WIFI_AWAKE=0','CSP_PRODUCT_SPEED2_PROFILE=0'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+define] if section=='env:tdongle_s3_product_speed2' and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,'tdongle_s3_product_speed2')
        def test_manifest_valid(self):
            p=self.root/'file';p.write_text('synthetic');verify_files(self.root,{'file':identity(p)})
        def test_manifest_corrupt(self):
            p=self.root/'file';p.write_text('synthetic');m={'file':identity(p)};p.write_text('corrupted');self.assertRaises(GuardError,verify_files,self.root,m)
        def test_manifest_escape(self):self.assertRaises(GuardError,verify_files,self.root,{'../outside':{'bytes':0,'sha256':''}})
        def test_manifest_missing(self):self.assertRaises(GuardError,verify_files,self.root,{'missing':{'bytes':0,'sha256':''}})
        def test_manifest_symlink_escape(self):
            sdk=self.root/'sdk';sdk.mkdir();outside=self.root/'outside';outside.write_text('unchanged');(sdk/'alias').symlink_to(outside)
            self.assertRaises(GuardError,verify_files,sdk,{'alias':identity(outside)})
        def test_unknown_defaults_preserved(self):
            s=self.root/'source';s.write_text('approved');d=self.root/'destination';d.write_text('uncommitted');self.assertRaises(GuardError,prepare_defaults,d,s,sha(s),True);self.assertEqual(d.read_text(),'uncommitted')
        def test_defaults_copy_and_repeat(self):
            s=self.root/'source';s.write_text('approved');d=self.root/'destination';prepare_defaults(d,s,sha(s),True);before=d.stat().st_mtime_ns;prepare_defaults(d,s,sha(s),True);self.assertEqual(d.stat().st_mtime_ns,before)
        def test_check_only_does_not_create(self):
            s=self.root/'source';s.write_text('approved');d=self.root/'destination';prepare_defaults(d,s,sha(s),False);self.assertFalse(d.exists())
        def test_source_defaults_corrupt(self):
            s=self.root/'source';s.write_text('not approved');self.assertRaises(GuardError,prepare_defaults,self.root/'new',s,'0'*64,True)
        def test_last_define_wins(self):self.assertEqual(defines(['-DX=0','-DX=1'])[0]['X'],'1')
        def product_config(self,flags=None):
            from platformio.project.config import ProjectConfig
            base=ProjectConfig(str(ROOT/'platformio.ini'));public='env:tdongle_s3_public_wifi_mem1'
            selected=base.get(public,'build_flags')+['-DCSP_PRODUCT_LAB_PROFILE=1','-DCSP_ENABLE_EVENT_LOG=1'] if flags is None else flags
            class SyntheticProductConfig:
                def get(self,section,option,**kwargs):
                    if section=='env:tdongle_s3_product_lab':
                        if option=='build_flags':return selected
                        section=public
                    return base.get(section,option,**kwargs)
                def has_option(self,section,option):return base.has_option(section,option)
            return SyntheticProductConfig()
        def test_fixed_project_cache_does_not_write_the_frozen_core(self):
            env=child_environment()
            self.assertEqual(env['PLATFORMIO_CACHE_DIR'],str(ROOT/'.pio/pio-cache'))
            self.assertTrue(Path(env['PLATFORMIO_CACHE_DIR']).is_relative_to(ROOT))
            self.assertEqual(env['PLATFORMIO_CORE_DIR'],str(BUILD_CORE))
        def test_release_lab_exact_wrappers_and_never_leaks(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            name='tdongle_s3_product_release_lab'
            config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
            self.assertEqual(check_configuration(config,board,name)['version'],'0.4.7-release-lab')
            for flag in ('CSP_PRODUCT_RELEASE_LAB_PROFILE','CSP_BLE_RELEASE_DIAGNOSTICS'):
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'=0'] if section=='env:'+name and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,name)
                for other in PROFILES:
                    if other==name:continue
                    config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'=1'] if section=='env:'+other and option=='build_flags' else original(section,option,**kw)
                    self.assertRaises(GuardError,check_configuration,config,board,other)
            for wrap in ('malloc','calloc','free','heap_caps_malloc','heap_caps_calloc','heap_caps_free'):
                config.get=lambda section,option,**kw:[value for value in original(section,option,**kw) if value!='-Wl,--wrap='+wrap] if section=='env:'+name and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,name)
        def test_profile_choice_and_separate_outputs(self):
            self.assertEqual(set(PROFILES),{'tdongle_s3_public_wifi_mem1','tdongle_s3_product_lab','tdongle_s3_product_lab2','tdongle_s3_product_lab3','tdongle_s3_product_lab4','tdongle_s3_product_lab5','tdongle_s3_product_lab6','tdongle_s3_product_lab7','tdongle_s3_product_rc1','tdongle_s3_product_dashboard1','tdongle_s3_product_speed1','tdongle_s3_product_rc2','tdongle_s3_product_rc3','tdongle_s3_product_rc4','tdongle_s3_product_rc5','tdongle_s3_product_setup_dev','tdongle_s3_product_incremental_dev','tdongle_s3_product_ui_dev','tdongle_s3_product_wifi_dev','tdongle_s3_product_ble_dev','tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab','tdongle_s3_product_speed2'})
            self.assertEqual(profile_for('tdongle_s3_public_wifi_mem1')['version'],'0.3.3-public-wifi-mem1')
            self.assertEqual(profile_for('tdongle_s3_product_lab')['version'],'0.4.0-product-lab1')
        def test_rc1_actual_configuration_and_nonfixture_identity(self):
            from platformio.project.config import ProjectConfig
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_rc1')
            self.assertEqual(result['version'],'0.4.0-rc1')
            self.assertEqual(result['required_flags']['CSP_BACKUP_FIXTURE_ONLY'],'0')
            self.assertEqual(result['required_flags']['CSP_NIMBLE_STOP_REPAIR'],'1')
        def test_dashboard_actual_configuration_and_identity(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'));board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            result=check_configuration(config,board,'tdongle_s3_product_dashboard1')
            self.assertEqual(result['version'],'0.4.0-dashboard1')
            self.assertEqual(result['required_flags']['CSP_ENABLE_LOCAL_DASHBOARD'],'1')
            self.assertEqual(result['required_flags']['CSP_BACKUP_FIXTURE_ONLY'],'0')
            for flag in ('CSP_ENABLE_LOCAL_DASHBOARD','CSP_PRODUCT_DASHBOARD_TRIAL'):
                candidate=ProjectConfig(str(ROOT/'platformio.ini'));original=candidate.get
                candidate.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'=0'] if section=='env:tdongle_s3_product_dashboard1' and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,candidate,board,'tdongle_s3_product_dashboard1')
        def test_dashboard_cannot_reuse_an_earlier_identity(self):
            from platformio.project.config import ProjectConfig
            for name in ('tdongle_s3_product_rc1','tdongle_s3_product_lab7'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-DCSP_PRODUCT_DASHBOARD_TRIAL=1','-DCSP_ENABLE_LOCAL_DASHBOARD=1'] if section=='env:'+name and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),name)
        def test_rc2_requires_both_corrected_features_and_its_identity(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),board,'tdongle_s3_product_rc2')
            self.assertEqual(result['version'],'0.4.0-rc2')
            for flag in ('CSP_PRODUCT_RC2_PROFILE','CSP_ENABLE_LOCAL_DASHBOARD','CSP_PRODUCT_DASHBOARD_TRIAL','CSP_UPLOAD_THROUGHPUT_TRIAL','CSP_PRODUCT_SPEED_TRIAL','CSP_BACKUP_WIFI_AWAKE'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'=0'] if section=='env:tdongle_s3_product_rc2' and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,'tdongle_s3_product_rc2')
            for name in ('tdongle_s3_product_rc1','tdongle_s3_product_speed1','tdongle_s3_product_dashboard1'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-DCSP_PRODUCT_RC2_PROFILE=1'] if section=='env:'+name and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,name)
        def test_rc3_exact_feature_profile_and_historical_identity_barriers(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            config=ProjectConfig(str(ROOT/'platformio.ini'))
            result=check_configuration(config,board,'tdongle_s3_product_rc3')
            self.assertEqual(result['version'],'0.4.0-rc3')
            for flag in PROFILES['tdongle_s3_product_rc3']['flags']:
                if PROFILES['tdongle_s3_product_rc3']['flags'][flag]!='1':continue
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'=0'] if section=='env:tdongle_s3_product_rc3' and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,'tdongle_s3_product_rc3')
            for name in ('tdongle_s3_product_rc1','tdongle_s3_product_rc2','tdongle_s3_product_speed2'):
                for flag in ('CSP_PRODUCT_RC3_PROFILE','CSP_ENABLE_OWNER_CLOUD_SETUP','CSP_ENABLE_OWNER_LOGIN','CSP_ENABLE_SCAN_IDLE_GATE','CSP_ENABLE_LCD_V2','CSP_DASHBOARD_PASSIVE_CLOSE'):
                    config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                    config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'=1'] if section=='env:'+name and option=='build_flags' else original(section,option,**kw)
                    self.assertRaises(GuardError,check_configuration,config,board,name)
        def test_rc4_exact_profile_preserves_rc3_and_rejects_mixed_identities(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            name='tdongle_s3_product_rc4'
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),board,name)
            self.assertEqual(result['version'],'0.4.0-rc4')
            old=PROFILES['tdongle_s3_product_rc3']['flags'];new=PROFILES[name]['flags']
            self.assertEqual({k:v for k,v in old.items() if k!='CSP_PRODUCT_RC3_PROFILE'},
                             {k:v for k,v in new.items() if k!='CSP_PRODUCT_RC4_PROFILE'})
            for flag,value in new.items():
                changed='1' if value=='0' else '4096' if flag=='CSP_UPLOAD_TX_BYTES' else '0'
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'='+changed] if section=='env:'+name and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,name)
            for profile in PROFILES:
                if profile==name:continue
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-DCSP_PRODUCT_RC4_PROFILE=1'] if section=='env:'+profile and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,profile)
            for flag in ('CSP_PRODUCT_RC3_PROFILE','CSP_PRODUCT_RC2_PROFILE','CSP_PRODUCT_SPEED2_PROFILE'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'=1'] if section=='env:'+name and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,name)
        def test_rc5_exact_profile_preserves_rc4_and_rejects_mixed_identities(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            name='tdongle_s3_product_rc5'
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),board,name)
            self.assertEqual(result['version'],'0.4.0-rc5')
            old=PROFILES['tdongle_s3_product_rc4']['flags'];new=PROFILES[name]['flags']
            self.assertEqual({k:v for k,v in old.items() if k!='CSP_PRODUCT_RC4_PROFILE'},
                             {k:v for k,v in new.items() if k!='CSP_PRODUCT_RC5_PROFILE'})
            for flag,value in new.items():
                changed='1' if value=='0' else '4096' if flag=='CSP_UPLOAD_TX_BYTES' else '0'
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'='+changed] if section=='env:'+name and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,name)
            for profile in PROFILES:
                if profile==name:continue
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-DCSP_PRODUCT_RC5_PROFILE=1'] if section=='env:'+profile and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,profile)
            for flag in ('CSP_PRODUCT_RC4_PROFILE','CSP_PRODUCT_RC3_PROFILE','CSP_PRODUCT_RC2_PROFILE','CSP_PRODUCT_SPEED2_PROFILE'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'=1'] if section=='env:'+name and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,name)
        def test_setup_development_exact_features_and_historical_isolation(self):
            from platformio.project.config import ProjectConfig
            board=json.loads((ROOT/'boards/tdongles3.json').read_text())
            name='tdongle_s3_product_setup_dev'
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),board,name)
            self.assertEqual(result['version'],'0.4.1-setup-dev')
            self.assertEqual(PROFILES[name]['output'],'artifacts/product-setup-dev-build')
            old=PROFILES['tdongle_s3_product_rc5']['flags'];new=PROFILES[name]['flags']
            self.assertEqual({k:v for k,v in old.items() if k!='CSP_PRODUCT_RC5_PROFILE'},
                             {k:v for k,v in new.items() if k!='CSP_PRODUCT_SETUP_DEV_PROFILE'})
            for flag,value in new.items():
                changed='1' if value=='0' else '4096' if flag=='CSP_UPLOAD_TX_BYTES' else '0'
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'='+changed] if section=='env:'+name and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,name)
            for profile in PROFILES:
                if profile==name:continue
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-DCSP_PRODUCT_SETUP_DEV_PROFILE=1'] if section=='env:'+profile and option=='build_flags' else original(section,option,**kw)
                if profile in ('tdongle_s3_product_incremental_dev','tdongle_s3_product_ui_dev','tdongle_s3_product_wifi_dev','tdongle_s3_product_ble_dev','tdongle_s3_product_cache_dev','tdongle_s3_product_cache_lab','tdongle_s3_product_release_lab'):
                    # This candidate intentionally inherits the setup base;
                    # distinct identity/cache bits are tested independently.
                    self.assertEqual(check_configuration(config,board,profile)['version'],PROFILES[profile]['version'])
                else:self.assertRaises(GuardError,check_configuration,config,board,profile)
            for flag in ('CSP_PRODUCT_RC5_PROFILE','CSP_PRODUCT_RC4_PROFILE','CSP_PRODUCT_RC3_PROFILE','CSP_PRODUCT_RC2_PROFILE','CSP_PRODUCT_SPEED2_PROFILE'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+flag+'=1'] if section=='env:'+name and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,board,name)
        def test_rc1_rejects_every_lab_or_instrumentation_override(self):
            from platformio.project.config import ProjectConfig
            for define in ('CSP_BACKUP_FIXTURE_ONLY=1','CSP_PRODUCT_LAB_PROFILE=1','CSP_PRODUCT_LAB7_PROFILE=1','CSP_PRODUCT_LAB5_PROFILE=1','CSP_PANIC_DIAGNOSTICS=1','CSP_UPLOAD_DIAGNOSTICS=1','CSP_ENABLE_REMOTE_HEALTH=0','CSP_NIMBLE_STOP_REPAIR=0'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kw:original(section,option,**kw)+['-D'+define] if section=='env:tdongle_s3_product_rc1' and option=='build_flags' else original(section,option,**kw)
                self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_rc1')
        def test_rc1_rejects_diagnostic_linker_wrapper(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
            config.get=lambda section,option,**kw:original(section,option,**kw)+['-Wl,--wrap=esp_panic_handler'] if section=='env:tdongle_s3_product_rc1' and option=='build_flags' else original(section,option,**kw)
            self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_rc1')
        def test_rc1_cannot_reuse_lab7_identity(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
            config.get=lambda section,option,**kw:original(section,option,**kw)+['-DCSP_PRODUCT_RC_PROFILE=1'] if section=='env:tdongle_s3_product_lab7' and option=='build_flags' else original(section,option,**kw)
            self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab7')
        def test_lab5_actual_configuration(self):
            from platformio.project.config import ProjectConfig
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab5')
            self.assertEqual(result['version'],'0.4.0-product-lab5')
            self.assertEqual(result['required_flags']['CSP_NIMBLE_STOP_REPAIR'],'1')
        def test_lab6_actual_configuration_and_exact_identity(self):
            from platformio.project.config import ProjectConfig
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab6')
            self.assertEqual(result['version'],'0.4.0-product-lab6')
            self.assertEqual(result['required_flags']['CSP_PRODUCT_LAB6_PROFILE'],'1')
            self.assertEqual(result['required_flags']['CSP_NIMBLE_STOP_REPAIR'],'1')
            self.assertNotEqual(profile_for('tdongle_s3_product_lab6')['output'],profile_for('tdongle_s3_product_lab5')['output'])
        def test_lab7_actual_configuration_and_exact_identity(self):
            from platformio.project.config import ProjectConfig
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab7')
            self.assertEqual(result['version'],'0.4.0-product-lab7')
            self.assertEqual(result['required_flags']['CSP_PRODUCT_LAB7_PROFILE'],'1')
            self.assertEqual(result['required_flags']['CSP_NIMBLE_STOP_REPAIR'],'1')
        def test_startup_identity_cannot_reuse_lab6(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
            config.get=lambda section,option,**kwargs:original(section,option,**kwargs)+['-DCSP_PRODUCT_LAB7_PROFILE=1'] if section=='env:tdongle_s3_product_lab6' and option=='build_flags' else original(section,option,**kwargs)
            self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab6')
        def test_route_diagnostics_cannot_reuse_lab5_identity(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
            config.get=lambda section,option,**kwargs:original(section,option,**kwargs)+['-DCSP_PRODUCT_LAB6_PROFILE=1'] if section=='env:tdongle_s3_product_lab5' and option=='build_flags' else original(section,option,**kwargs)
            self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab5')
        def test_lab5_each_repair_identity_flag_required(self):
            from platformio.project.config import ProjectConfig
            for flag in ('CSP_NIMBLE_STOP_REPAIR','CSP_PRODUCT_LAB5_PROFILE'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kwargs: original(section,option,**kwargs)+['-D'+flag+'=0'] if section=='env:tdongle_s3_product_lab5' and option=='build_flags' else original(section,option,**kwargs)
                self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab5')
        def test_earlier_identity_cannot_enable_repair(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
            config.get=lambda section,option,**kwargs: original(section,option,**kwargs)+['-DCSP_NIMBLE_STOP_REPAIR=1'] if section=='env:tdongle_s3_product_lab4' and option=='build_flags' else original(section,option,**kwargs)
            self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab4')
            self.assertNotEqual(profile_for('tdongle_s3_public_wifi_mem1')['output'],profile_for('tdongle_s3_product_lab')['output'])
            self.assertRaises(GuardError,profile_for,'unknown')
        def test_unknown_environment_rejected_before_work(self):
            existing=ROOT/'artifacts/public-wifi-memory-build/MANIFEST.json';before=sha(existing) if existing.exists() else None
            process=subprocess.run([sys.executable,str(Path(__file__).resolve()),'--environment','unknown','--check-only'],capture_output=True,text=True)
            self.assertEqual(process.returncode,2,process.stderr);self.assertEqual(sha(existing) if existing.exists() else None,before)
        def test_synthetic_product_configuration(self):
            result=check_configuration(self.product_config(),json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab')
            self.assertEqual(result['version'],'0.4.0-product-lab1');self.assertEqual(result['required_flags']['CSP_PRODUCT_LAB_PROFILE'],'1');self.assertEqual(result['required_flags']['CSP_ENABLE_EVENT_LOG'],'1')
        def test_product_new_flags_are_mandatory(self):
            from platformio.project.config import ProjectConfig
            base=ProjectConfig(str(ROOT/'platformio.ini')).get('env:tdongle_s3_public_wifi_mem1','build_flags')
            for flags in (base,base+['-DCSP_PRODUCT_LAB_PROFILE=1'],base+['-DCSP_ENABLE_EVENT_LOG=1'],base+['-DCSP_PRODUCT_LAB_PROFILE=0','-DCSP_ENABLE_EVENT_LOG=1']):
                self.assertRaises(GuardError,check_configuration,self.product_config(flags),json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab')
        def test_product_flags_cannot_reuse_public_identity(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
            config.get=lambda section,option,**kwargs:original(section,option,**kwargs)+['-DCSP_PRODUCT_LAB_PROFILE=1','-DCSP_ENABLE_EVENT_LOG=1'] if section=='env:tdongle_s3_public_wifi_mem1' and option=='build_flags' else original(section,option,**kwargs)
            self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_public_wifi_mem1')
        def test_actual_product_configuration_when_available(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'))
            if not config.has_section('env:tdongle_s3_product_lab'):self.skipTest('Root product environment not present yet; no PIO build attempted')
            self.assertEqual(check_configuration(config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab')['version'],'0.4.0-product-lab1')
        def test_actual_clock_correction_configuration(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'))
            result=check_configuration(config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab2')
            self.assertEqual(result['version'],'0.4.0-product-lab2')
            self.assertEqual(result['required_flags']['CSP_PRODUCT_LAB2_PROFILE'],'1')
            self.assertNotEqual(profile_for('tdongle_s3_product_lab2')['output'],profile_for('tdongle_s3_product_lab')['output'])
        def test_actual_transfer_diagnostic_configuration(self):
            from platformio.project.config import ProjectConfig
            result=check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab3')
            self.assertEqual(result['version'],'0.4.0-product-lab3')
            self.assertEqual(result['required_flags']['CSP_UPLOAD_DIAGNOSTICS'],'1')
            self.assertNotEqual(profile_for('tdongle_s3_product_lab3')['output'],profile_for('tdongle_s3_product_lab2')['output'])
        def test_diagnostics_cannot_reuse_older_image_identity(self):
            from platformio.project.config import ProjectConfig
            for target in ('tdongle_s3_product_lab','tdongle_s3_product_lab2','tdongle_s3_public_wifi_mem1'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kwargs:original(section,option,**kwargs)+['-DCSP_UPLOAD_DIAGNOSTICS=1'] if section=='env:'+target and option=='build_flags' else original(section,option,**kwargs)
                self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),target)
        def test_each_diagnostic_wrap_is_required(self):
            from platformio.project.config import ProjectConfig
            for name in ('mbedtls_ssl_write','mbedtls_ssl_read','mbedtls_net_send'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kwargs:[flag for flag in original(section,option,**kwargs) if flag!='-Wl,--wrap='+name] if section=='env:tdongle_s3_product_lab3' and option=='build_flags' else original(section,option,**kwargs)
                self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab3')
        def test_current_configuration(self):
            from platformio.project.config import ProjectConfig
            self.assertEqual(check_configuration(ProjectConfig(str(ROOT/'platformio.ini')),json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_public_wifi_mem1')['cache_key'],'52ba1142b3ef9bf7')
        def test_lab4_requires_both_bounded_features(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'))
            result=check_configuration(config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab4')
            self.assertEqual(result['version'],'0.4.0-product-lab4')
            original=config.get
            for name in ('CSP_PRODUCT_LAB4_PROFILE','CSP_ENABLE_FILE_HISTORY','CSP_ENABLE_REMOTE_HEALTH'):
                config.get=lambda section,option,**kwargs:[flag for flag in original(section,option,**kwargs) if flag!='-D'+name+'=1'] if section=='env:tdongle_s3_product_lab4' and option=='build_flags' else original(section,option,**kwargs)
                self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab4')
            config.get=original
        def test_earlier_id_cannot_enable_history_or_health(self):
            from platformio.project.config import ProjectConfig
            for name in ('CSP_PRODUCT_LAB4_PROFILE','CSP_ENABLE_FILE_HISTORY','CSP_ENABLE_REMOTE_HEALTH'):
                config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
                config.get=lambda section,option,**kwargs:[*original(section,option,**kwargs),'-D'+name+'=1'] if section=='env:tdongle_s3_product_lab3' and option=='build_flags' else original(section,option,**kwargs)
                self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_product_lab3')
        def test_exact_readonly_observer_only(self):
            base='prefix\nbool storage_media_detected() {\nold();\n}\n'
            approved=base.replace('bool storage_media_detected() {',HEALTH_STORAGE_OBSERVER+'bool storage_media_detected() {')
            self.assertFalse(verify_storage_source(base,base))
            self.assertTrue(verify_storage_source(approved,base,True))
            self.assertRaises(GuardError,verify_storage_source,approved,base,False)
            self.assertRaises(GuardError,verify_storage_source,approved.replace('old();','changed();'),base,True)
            self.assertRaises(GuardError,verify_storage_source,approved.replace('xSemaphoreTake(mutex,0)','xSemaphoreTake(mutex,portMAX_DELAY)'),base,True)
        def test_changed_sdk_option_rejected(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
            config.get=lambda section,option,**kwargs:OPTIONS.replace('4096','2048') if option=='custom_sdkconfig' else original(section,option,**kwargs)
            self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_public_wifi_mem1')
        def test_changed_memory_fingerprint_rejected(self):
            from platformio.project.config import ProjectConfig
            config=ProjectConfig(str(ROOT/'platformio.ini'));original=config.get
            config.get=lambda section,option,**kwargs:'opi' if option=='board_build.psram_type' else original(section,option,**kwargs)
            self.assertRaises(GuardError,check_configuration,config,json.loads((ROOT/'boards/tdongles3.json').read_text()),'tdongle_s3_public_wifi_mem1')
        def test_actual_version_branches(self):
            text=(ROOT/'include/pro_device.h').read_text();start=text.index('#if ',text.index('#include "ble_feature.h"'));end=text.index('\n#endif',start)+len('\n#endif');block=text[start:end]
            compiler='/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr/lib/llvm-18/bin/clang++'
            cases=[([], '0.3.3-dev'),(['CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=1'],'0.3.3-public-wifi-lab1'),(['CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=1','CSP_PUBLIC_WIFI_MEMORY_PROFILE=1'],'0.3.3-public-wifi-mem1'),(['CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=0','CSP_PUBLIC_WIFI_MEMORY_PROFILE=1'],'0.3.3-dev'),(['CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=1','CSP_PUBLIC_WIFI_MEMORY_PROFILE=0'],'0.3.3-public-wifi-lab1')]
            cases += [(['CSP_PRODUCT_RC3_PROFILE=1'],'0.4.0-rc3'),(['CSP_PRODUCT_RC4_PROFILE=1'],'0.4.0-rc4'),(['CSP_PRODUCT_RC5_PROFILE=1'],'0.4.0-rc5'),(['CSP_PRODUCT_SETUP_DEV_PROFILE=1'],'0.4.1-setup-dev')]
            for flags,version in cases:
                output=subprocess.run([compiler,'-E','-P','-x','c++',*[('-D'+flag) for flag in flags],'-'],input=block,capture_output=True,text=True,check=True).stdout
                self.assertIn('FirmwareVersion="'+version+'"',output)
        def test_actual_product_version_when_available(self):
            text=(ROOT/'include/pro_device.h').read_text()
            if '0.4.0-product-lab1' not in text:self.skipTest('Root product version branch not present yet')
            start=text.index('#if ',text.index('#include "ble_feature.h"'));end=text.index('\n#endif',start)+len('\n#endif');block=text[start:end]
            compiler='/config/.unmanic/research/cloud-stick-auth-native-deps/root/usr/lib/llvm-18/bin/clang++'
            flags=['CSP_ENABLE_PUBLIC_WIFI_ASSISTANCE=1','CSP_PUBLIC_WIFI_MEMORY_PROFILE=1','CSP_PRODUCT_LAB_PROFILE=1','CSP_ENABLE_EVENT_LOG=1']
            output=subprocess.run([compiler,'-E','-P','-x','c++',*[('-D'+flag) for flag in flags],'-'],input=block,capture_output=True,text=True,check=True).stdout
            self.assertIn('FirmwareVersion="0.4.0-product-lab1"',output)
        def test_metadata_patch_only_redirects_backups(self):
            text=redirected_metadata_source()
            self.assertEqual(text.count(' / ".pio" / f"mem1-pioarduino-build.py.'),4)
            self.assertNotIn('shutil.copy2(backup_path, build_py_path)',text)
            self.assertIn('Immutable SDK build script differs',text)
        def test_landlock_denies_write_through_stage_sdk_alias(self):
            allowed=self.root/'allowed';allowed.mkdir();protected=self.root/'protected';protected.mkdir();old=protected/'old';old.write_text('unchanged');(allowed/'sdk').symlink_to(protected,target_is_directory=True)
            code='''from pathlib import Path
import sys
for target in (Path(sys.argv[1])/'sdk'/'old',Path(sys.argv[1])/'sdk'/'new'):
 try:target.write_text('forbidden')
 except PermissionError:continue
 raise SystemExit('protected alias write allowed')
'''
            env=os.environ.copy();env['PYTHONDONTWRITEBYTECODE']='1'
            process=subprocess.run([sys.executable,'-c',code,str(allowed)],env=env,capture_output=True,text=True,preexec_fn=lambda:Landlock.restrict([allowed]))
            self.assertEqual(process.returncode,0,process.stderr);self.assertEqual(old.read_text(),'unchanged');self.assertFalse((protected/'new').exists())
        def test_landlock_denies_existing_and_new_protected_write(self):
            self.assertGreaterEqual(Landlock.abi(),3)
            allowed=self.root/'allowed';allowed.mkdir();protected=self.root/'protected';protected.mkdir();old=protected/'old';old.write_text('unchanged')
            code='''from pathlib import Path
import sys
allowed,protected=map(Path,sys.argv[1:]);(allowed/'new').write_text('ok')
for target in (protected/'old',protected/'new'):
 try:target.write_text('forbidden')
 except PermissionError:continue
 raise SystemExit('protected write allowed')
'''
            env=os.environ.copy();env['PYTHONDONTWRITEBYTECODE']='1'
            p=subprocess.run([sys.executable,'-c',code,str(allowed),str(protected)],env=env,capture_output=True,text=True,preexec_fn=lambda:Landlock.restrict([allowed]))
            self.assertEqual(p.returncode,0,p.stderr);self.assertEqual(old.read_text(),'unchanged');self.assertFalse((protected/'new').exists());self.assertEqual((allowed/'new').read_text(),'ok')
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(Tests);result=unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1

if __name__=='__main__':
    try:sys.exit(main())
    except GuardError as error:print('GUARD FAILED:',error,file=sys.stderr);sys.exit(1)
