#include "pro_event_log.h"
#include <cassert>
int main(){
  const auto page=pro::events::readPage(4294967295u);
  assert(!page.count&&!page.more&&!page.status.loaded&&!page.status.dirty);
  assert(pro::events::workerLoad()==pro::events::Load::Disabled);
  assert(!pro::events::record(pro::events::Kind::Error,-1,0,0,0));
}
