#include "platform/android13/pending_suspend_deopt.h"
#include <atomic>
#include <cassert>
#include <thread>
using namespace artdcevm::android13;
int main() {
  PendingSuspendDeopt p; int a=0,b=0;
  assert(!p.Mark(nullptr));assert(p.Mark(&a));assert(!p.Mark(&b));
  assert(!p.Consume(&b)&&p.IsPending(&a));assert(p.Consume(&a));
  assert(!p.Cancel(&a)&&!p.Mark(&b));p.Reset();assert(p.State(&a)==SuspendDeoptState::kClaimed);
  assert(p.Execute(&a));assert(!p.Complete(&b));assert(!p.Cancel(&a));p.Reset();
  assert(p.State(&a)==SuspendDeoptState::kExecuting&&p.InFlight());
  assert(p.Complete(&a)&&!p.IsPending(&a)&&!p.InFlight());assert(!p.Complete(&a));
  assert(p.Release(&a));assert(p.Mark(&b));assert(p.Cancel(&b));assert(!p.Consume(&b));
  assert(p.Release(&b));assert(p.Mark(&a));assert(p.Consume(&a));assert(p.Reject(&a));
  assert(p.Mark(&b));p.Reset();assert(!p.IsPending(&b));
  // Simultaneous claim and cancel have one winner; neither can free an executing slot.
  for(int i=0;i<1000;++i) {
    assert(p.Mark(&a));std::atomic<bool> start{false};bool claimed=false,cancelled=false;
    std::thread first([&]{while(!start.load()){}claimed=p.Consume(&a);});
    std::thread second([&]{while(!start.load()){}cancelled=p.Cancel(&a);});
    start=true;first.join();second.join();assert(claimed!=cancelled);
    if(claimed){assert(p.Execute(&a));assert(!p.Cancel(&a));assert(p.Complete(&a));}
    assert(p.Release(&a));
  }
  // Same-thread immediate reuse cannot accept old completion, cancellation or release.
  uint64_t old_token,new_token,claimed_token;
  assert(p.Mark(&a,&old_token));assert(p.Consume(&a,&claimed_token)&&claimed_token==old_token);
  assert(p.Execute(&a,old_token));assert(p.Complete(&a,old_token));
  assert(p.Mark(&a,&new_token)&&new_token!=old_token);
  assert(!p.Release(&a,old_token)&&!p.Cancel(&a,old_token));
  assert(p.Consume(&a,&claimed_token)&&claimed_token==new_token);assert(p.Execute(&a,new_token));
  assert(!p.Complete(&a,old_token));assert(p.Complete(&a,new_token));assert(p.Release(&a,new_token));
  assert(p.Mark(&a,&old_token));assert(p.Consume(&a,&claimed_token));assert(p.Reject(&a,old_token));
  assert(p.Mark(&a,&new_token));assert(!p.Release(&a,old_token)&&!p.Reject(&a,old_token));p.Reset();
  std::atomic<bool> start{false};bool first=false,second=false;
  std::thread x([&]{while(!start.load()){}first=p.Mark(&a);});
  std::thread y([&]{while(!start.load()){}second=p.Mark(&b);});
  start=true;x.join();y.join();assert(first!=second);p.Reset();
}
