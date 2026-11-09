#include "cloud_request_trace.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <type_traits>
std::atomic<uint32_t> traceNow{0};
static_assert(std::is_standard_layout_v<CloudRequestSnapshot>);
static_assert(sizeof(CloudRequestSnapshot)<=128);
static_assert(!std::is_constructible_v<CloudRequestTrace,const char*>);
static_assert(!std::is_copy_constructible_v<CloudRequestTrace>);
int main(){
 assert(!cloud_request_snapshot().active&&cloud_request_snapshot().enabled);
 traceNow=UINT32_MAX-10;
 {
  CloudRequestTrace request(CloudRequestOp::FinalReadback);request.headers(200);request.body(-4);traceNow=9;
  const auto result=request.finish({false,-4});assert(!result.ok&&result.code==-4);
 }
 auto first=cloud_request_snapshot();assert(!first.active&&first.last.elapsed_ms==20);
 assert(first.last.http_code==200&&first.last.body_result==-4&&first.last.result_code==-4);
 const auto negativeId=first.last_negative.id;
 {
  CloudRequestTrace success(CloudRequestOp::FolderCheck);success.headers(207);success.finish({true,207});
 }
 auto s=cloud_request_snapshot();assert(s.last.operation==CloudRequestOp::FolderCheck&&s.last_negative.id==negativeId);
 {
  CloudRequestTrace outer(CloudRequestOp::StageReadback);const auto id=cloud_request_snapshot().current.id;
  {CloudRequestTrace accidental(CloudRequestOp::Publish);accidental.headers(500);accidental.finish({false,500});}
  assert(cloud_request_snapshot().active&&cloud_request_snapshot().current.id==id);
  outer.headers(404);outer.finish({false,404});
 }
 assert(cloud_request_snapshot().last_negative.id==negativeId);
 {CloudRequestTrace interrupted(CloudRequestOp::StagePut);}
 assert(cloud_request_snapshot().last_negative.result_code==pro::SnapshotChanged&&!cloud_request_snapshot().active);
 std::atomic<bool> finished{false};
 std::thread writer([&]{for(int i=0;i<2000;++i){traceNow.fetch_add(1);CloudRequestTrace event(CloudRequestOp::StageReadback);event.headers(200);event.body(-4);event.finish({false,-4});}finished=true;});
 do{
  const auto snapshot=cloud_request_snapshot();
  if(snapshot.last_negative.result_code==-4){assert(snapshot.last_negative.http_code==200&&snapshot.last_negative.body_result==-4);}
  if(snapshot.active)assert(snapshot.current.id&&snapshot.current.operation==CloudRequestOp::StageReadback);
 }while(!finished);
 writer.join();assert(!cloud_request_snapshot().active);
 std::cout<<"PASS numeric-only request outcomes, clock wrap, retained errors, nested exclusion and coherent cross-thread snapshots\n";
}
