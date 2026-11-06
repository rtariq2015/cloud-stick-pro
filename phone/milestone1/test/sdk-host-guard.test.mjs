import {test} from 'node:test';
import assert from 'node:assert/strict';
import {sdkHost} from './sdk_host.mjs';
for(const diagnostic of ['ERROR: AddressSanitizer: injected','runtime error: injected alignment','UndefinedBehaviorSanitizer: injected','LeakSanitizer: injected'])test('zero-exit SDK child cannot hide '+diagnostic,async()=>{
 const source='process.stdin.resume();process.stdin.once("data",()=>{process.stderr.write('+JSON.stringify(diagnostic+'\n')+');process.exitCode=0;process.stdin.pause();});';
 const host=sdkHost(process.execPath,['-e',source]);await assert.rejects(host.close(),/sanitizer diagnostic/);
});
test('SDK child receives mandatory strict sanitizer options',async()=>{
 const source='process.stdin.resume();process.stdin.once("data",()=>{if(!process.env.ASAN_OPTIONS.includes("halt_on_error=1:abort_on_error=1:detect_leaks=1")||!process.env.UBSAN_OPTIONS.includes("halt_on_error=1:print_stacktrace=1"))process.exitCode=7;process.stdin.pause();});';
 await sdkHost(process.execPath,['-e',source]).close();
});
