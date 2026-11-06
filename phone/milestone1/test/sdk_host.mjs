import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
export const sanitizerDiagnostic=/AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|runtime error:/i;
export function sdkHost(binary,args=[]){
 const env={...process.env,ASAN_OPTIONS:[process.env.ASAN_OPTIONS,'halt_on_error=1:abort_on_error=1:detect_leaks=1'].filter(Boolean).join(':'),UBSAN_OPTIONS:[process.env.UBSAN_OPTIONS,'halt_on_error=1:print_stacktrace=1'].filter(Boolean).join(':')};
 const child=spawn(binary,args,{stdio:['pipe','pipe','pipe'],env}),queue=[];let stderr='',failure=null,closing=false;
 const rejectPending=error=>{for(const q of queue.splice(0)){clearTimeout(q.timer);q.reject(error);}};
 child.stderr.on('data',data=>{stderr+=data;if(sanitizerDiagnostic.test(stderr)){failure=new Error('SDK sanitizer diagnostic: '+stderr);rejectPending(failure);}});
 child.on('error',error=>{failure=error;rejectPending(error);});
 const closed=new Promise(resolve=>child.once('close',(code,signal)=>{rejectPending(failure||new Error('SDK server exited '+code+': '+stderr));resolve({code,signal});}));
 createInterface({input:child.stdout}).on('line',line=>{const next=queue.shift();if(next){clearTimeout(next.timer);try{if(failure)throw failure;next.resolve(JSON.parse(line));}catch(error){next.reject(error);}}});
 return {async call(command,data){if(failure)throw failure;if(closing)throw new Error('SDK server is closing');return new Promise((resolve,reject)=>{const row={resolve,reject,timer:setTimeout(()=>{reject(new Error('SDK server timed out'));child.kill();},10000)};queue.push(row);child.stdin.write(command+(data?' '+Buffer.from(data).toString('hex'):'')+'\n',error=>{if(error){failure=error;rejectPending(error);}});});},async close(){closing=true;if(child.exitCode===null&&!child.killed)child.stdin.end('quit\n');const {code,signal}=await closed;if(failure)throw failure;if(sanitizerDiagnostic.test(stderr))throw new Error('SDK sanitizer diagnostic: '+stderr);if(stderr)throw new Error('Unexpected SDK stderr: '+stderr);if(code!==0)throw new Error('SDK server cleanup failed '+code+'/'+signal+': '+stderr);}};
}
