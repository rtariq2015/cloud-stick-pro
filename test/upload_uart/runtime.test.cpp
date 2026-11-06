// Actual UART handler/owner loop; all worker/radio/USB/NVS are synthetic.
#include "runtime_fixture_prefix.h"
#include "upload_diagnostics.h"
#include <array>
struct U64Field {const char* name;uint64_t UploadDiagnosticSnapshot::*member;};
struct U32Field {const char* name;uint32_t UploadDiagnosticSnapshot::*member;};
struct I32Field {const char* name;int32_t UploadDiagnosticSnapshot::*member;};
struct BoolField {const char* name;bool UploadDiagnosticSnapshot::*member;};
constexpr U64Field wide[]={
 {"expected_bytes",&UploadDiagnosticSnapshot::expected_bytes},{"source_fread_bytes",&UploadDiagnosticSnapshot::source_fread_bytes},
 {"source_returned_bytes",&UploadDiagnosticSnapshot::source_returned_bytes},{"tls_requested_bytes",&UploadDiagnosticSnapshot::tls_requested_bytes},
 {"tls_accepted_bytes",&UploadDiagnosticSnapshot::tls_accepted_bytes},{"net_requested_bytes",&UploadDiagnosticSnapshot::net_requested_bytes},
 {"net_accepted_bytes",&UploadDiagnosticSnapshot::net_accepted_bytes}};
constexpr U32Field counters[]={
 {"attempt",&UploadDiagnosticSnapshot::attempt},{"request_ms",&UploadDiagnosticSnapshot::request_ms},{"socket_timeout_ms",&UploadDiagnosticSnapshot::socket_timeout_ms},
 {"source_calls",&UploadDiagnosticSnapshot::source_calls},{"short_reads",&UploadDiagnosticSnapshot::short_reads},{"max_read_ms",&UploadDiagnosticSnapshot::max_read_ms},
 {"tls_write_calls",&UploadDiagnosticSnapshot::tls_write_calls},{"tls_read_calls",&UploadDiagnosticSnapshot::tls_read_calls},{"net_send_calls",&UploadDiagnosticSnapshot::net_send_calls},
 {"want_read",&UploadDiagnosticSnapshot::want_read},{"want_write",&UploadDiagnosticSnapshot::want_write},{"max_tls_write_ms",&UploadDiagnosticSnapshot::max_tls_write_ms},
 {"last_positive_age_ms",&UploadDiagnosticSnapshot::last_positive_age_ms}};
constexpr I32Field codes[]={
 {"http_code",&UploadDiagnosticSnapshot::http_code},{"source_errno",&UploadDiagnosticSnapshot::source_errno},{"tls_last_write_ret",&UploadDiagnosticSnapshot::tls_last_write_ret},
 {"tls_first_fatal_write",&UploadDiagnosticSnapshot::tls_first_fatal_write},{"tls_last_read_ret",&UploadDiagnosticSnapshot::tls_last_read_ret},
 {"tls_first_fatal_read",&UploadDiagnosticSnapshot::tls_first_fatal_read},{"net_last_ret",&UploadDiagnosticSnapshot::net_last_ret},
 {"net_first_fatal",&UploadDiagnosticSnapshot::net_first_fatal},{"net_errno",&UploadDiagnosticSnapshot::net_errno},
 {"tls_write_errno",&UploadDiagnosticSnapshot::tls_write_errno},{"tls_read_errno",&UploadDiagnosticSnapshot::tls_read_errno}};
constexpr BoolField flags[]={
 {"enabled",&UploadDiagnosticSnapshot::enabled},{"active",&UploadDiagnosticSnapshot::active},{"completed",&UploadDiagnosticSnapshot::completed},
 {"source_ferror",&UploadDiagnosticSnapshot::source_ferror},{"source_eof",&UploadDiagnosticSnapshot::source_eof}};
static_assert(std::size(wide)+std::size(counters)+std::size(codes)+std::size(flags)+2==38);
#if CSP_UPLOAD_DIAGNOSTICS
UploadDiagnosticSnapshot fixtureSnapshot;
unsigned snapshotReads=0;
UploadDiagnosticSnapshot upload_diagnostics_snapshot(){++snapshotReads;return fixtureSnapshot;}
#endif
void fixture(const char* path){
 std::ifstream source(path,std::ios::binary);std::vector<uint8_t> owner{std::istreambuf_iterator<char>(source),{}};assert(owner.size()==432);
 testNvs["csp-owner/owner"]=owner;const std::string config="{\"schema\":1,\"private_fixture\":\"UNCHANGED_PRIVATE_CONFIG\"}";testNvs["csp/config"]={config.begin(),config.end()};
 pro::wifi_setup::StoredNetworks saved;saved.revision=2;saved.count=1;saved.selected=1;saved.next_id=2;
 auto& p=saved.profiles[0];p.id=1;std::strcpy(p.ssid.data(),"PRIVATE_SYNTHETIC_WIFI");std::strcpy(p.secret.data(),"PRIVATE_SYNTHETIC_PASSWORD");
 WifiNvsStore nvs;pro::wifi_setup::ProfileStore store(nvs);assert(store.publish(saved,1,[](void*){return true;},nullptr)==pro::wifi_setup::Published::Committed);
 setup();tick();assert(workerStarts==1&&!maintenance&&managed.phase==BackupWorkerPhase::Running);
 std::strcpy(observedBackup.currentFile,"PRIVATE_SYNTHETIC_FILE.pdf");std::strcpy(observedBackup.message,"PRIVATE_SYNTHETIC_ACCOUNT_SECRET");observedBackup.active=true;
}
std::vector<std::string> uart(const std::string& input){
 testNow+=300;const auto before=USBSerial.lines.size();USBSerial.send(input);while(USBSerial.available())loop();
 return {USBSerial.lines.begin()+before,USBSerial.lines.end()};
}
void rejected(const std::vector<std::string>& lines){
 if(lines.size()!=1){std::cerr<<"Unexpected UART lines="<<lines.size()<<"\n";for(const auto& line:lines)std::cerr<<line<<"\n";}
 assert(lines.size()==1);JsonDocument data;assert(!deserializeJson(data,lines[0]));
 assert(data.size()==1&&data["accepted"].is<bool>()&&data["accepted"]==false);
}
void exactSnapshot(const std::string& line,const UploadDiagnosticSnapshot& expected){
 JsonDocument data;assert(!deserializeJson(data,line));assert(data.size()==38);
 std::set<std::string> allowed;
 for(const auto& field:wide){allowed.insert(field.name);const auto v=data[field.name];assert(v.is<uint64_t>()&&!v.is<bool>()&&!v.is<const char*>());assert(v.as<uint64_t>()==expected.*(field.member));}
 for(const auto& field:counters){allowed.insert(field.name);const auto v=data[field.name];assert(v.is<uint32_t>()&&!v.is<bool>()&&!v.is<const char*>());assert(v.as<uint32_t>()==expected.*(field.member));}
 for(const auto& field:codes){allowed.insert(field.name);const auto v=data[field.name];assert(v.is<int32_t>()&&!v.is<bool>()&&!v.is<const char*>());assert(v.as<int32_t>()==expected.*(field.member));}
 for(const auto& field:flags){allowed.insert(field.name);assert(data[field.name].is<bool>()&&data[field.name].as<bool>()==expected.*(field.member));}
 allowed.insert("target");allowed.insert("source_abort");assert(data["target"].is<unsigned>()&&data["target"].as<unsigned>()==expected.target);assert(data["source_abort"].is<unsigned>()&&data["source_abort"].as<unsigned>()==expected.source_abort);
 for(const auto field:data.as<JsonObject>())assert(allowed.count(field.key().c_str())==1);
 assert(line.find("PRIVATE_SYNTHETIC")==std::string::npos&&line.find("UNCHANGED_PRIVATE_CONFIG")==std::string::npos);
 assert(line.find("http:")==std::string::npos&&line.find("https:")==std::string::npos);
}
int main(int argc,char** argv){
 assert(argc==2);fixture(argv[1]);const auto nvs=testNvs;const auto writes=nvsWrites;
 const auto worker=managed;const auto starts=workerStarts,retires=retireRequests,reaps=reapCalls,initializes=usbInitializations,reads=configReads,connections=radioConnections,checks=cloudChecks;
 const auto config=lastStartedConfig;const auto previousEvents=events.size();unsigned cases=0;
#if CSP_UPLOAD_DIAGNOSTICS
 const uint64_t large[]={UINT64_MAX,UINT64_MAX-1,9007199254740993ULL,9007199254740995ULL,0x100000000ULL,0x8000000000000000ULL,UINT64_MAX-2};
 for(unsigned i=0;i<std::size(wide);++i)fixtureSnapshot.*(wide[i].member)=large[i];
 for(unsigned i=0;i<std::size(counters);++i)fixtureSnapshot.*(counters[i].member)=UINT32_MAX-i*31;
 for(unsigned i=0;i<std::size(codes);++i)fixtureSnapshot.*(codes[i].member)=i?int32_t(-0x6880-int32_t(i)*137):INT32_MIN;
 fixtureSnapshot.tls_write_errno=11;fixtureSnapshot.tls_read_errno=104;fixtureSnapshot.source_errno=5;
 for(unsigned i=0;i<std::size(flags);++i)fixtureSnapshot.*(flags[i].member)=i%2==0;
 fixtureSnapshot.target=255;fixtureSnapshot.source_abort=4;
 auto result=uart("UPLOAD_STATUS");assert(result.size()==1&&snapshotReads==1);exactSnapshot(result[0],fixtureSnapshot);++cases;
 fixtureSnapshot={};result=uart("UPLOAD_STATUS");assert(result.size()==1&&snapshotReads==2);exactSnapshot(result[0],fixtureSnapshot);++cases;
 fixtureSnapshot.http_code=507;fixtureSnapshot.enabled=true;fixtureSnapshot.active=true;fixtureSnapshot.completed=true;fixtureSnapshot.source_eof=true;fixtureSnapshot.target=2;
 result=uart("UPLOAD_STATUS");assert(result.size()==1&&snapshotReads==3);exactSnapshot(result[0],fixtureSnapshot);++cases;
#else
 rejected(uart("UPLOAD_STATUS"));++cases;
#endif
 for(const char* unknown:{"UPLOAD_STATUS 1","UPLOAD_STATUSsuffix","upload_status"}){rejected(uart(unknown));++cases;}
 // Control characters take the existing overflow/drop path before dispatch.
 assert(uart("UPLOAD_STATUS\t").empty());++cases;
#if CSP_UPLOAD_DIAGNOSTICS
 assert(snapshotReads==3);
#endif
 assert(testNvs==nvs&&nvsWrites==writes&&workerStarts==starts&&retireRequests==retires&&reapCalls==reaps&&usbInitializations==initializes&&initializes==1&&configReads==reads&&radioConnections==connections&&cloudChecks==checks);
 assert(managed.phase==worker.phase&&managed.generation==worker.generation&&managed.retired_generation==worker.retired_generation&&managed.error==worker.error);
 assert(lastStartedConfig.server_url==config.server_url&&lastStartedConfig.username==config.username&&lastStartedConfig.password==config.password&&lastStartedConfig.ssid==config.ssid&&lastStartedConfig.wifi_password==config.wifi_password&&lastStartedConfig.ca_pem==config.ca_pem&&lastStartedConfig.watch_folder==config.watch_folder);
 assert(maintenance==false&&!bleRunning&&observedBackup.active&&events.size()==previousEvents);
 std::cout<<"PASS actual UART upload_diagnostics="<<CSP_UPLOAD_DIAGNOSTICS<<" cases="<<cases<<"; numeric-only response, unchanged NVS/worker/config/USB initialization\n";
}
