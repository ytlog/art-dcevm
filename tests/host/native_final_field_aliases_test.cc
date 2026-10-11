#include "platform/common/native_final_field_aliases.h"
#include "platform/common/source_commit_plan_identity.h"
#include "platform/common/source_commit.h"
#include <cassert>
#include <iostream>
#include <future>
#include <atomic>
#include <chrono>
using namespace artdcevm::final_field_aliases;
struct Weak {void* current=nullptr;bool readable=true;};struct Old {void* owner=nullptr;};
struct ReaderState {int metadata_reads=0,method_reads=0;void* field=nullptr;void* method=nullptr;Set* aliases=nullptr;void* current=nullptr;void* historical=nullptr;void* moved_current=nullptr;void* moved_historical=nullptr;void* thread=nullptr;bool suspend=false;std::promise<void>* started=nullptr;std::shared_future<void> proceed;};
bool Decode(void*,void* weak,void** value){auto& w=*static_cast<Weak*>(weak);*value=w.current;return w.readable;}
void* Owner(void* raw,void* old){++static_cast<ReaderState*>(raw)->metadata_reads;return static_cast<Old*>(old)->owner;}
bool DecodeThread(void* raw,void* thread,void* weak,void** value){auto& s=*static_cast<ReaderState*>(raw);assert(thread==s.thread);
 assert(s.aliases->WeakHandles().size()>0); // no alias mutex may span decoder
 if(s.started){s.started->set_value();s.proceed.wait();}
 if(s.suspend){s.current=s.moved_current;s.historical=s.moved_historical;static_cast<Weak*>(weak)->current=s.current;}
 return Decode(raw,weak,value);
}
void* Current(void* p,void* member){auto&s=*static_cast<ReaderState*>(p);return member==s.field?s.current:Owner(p,member);}
void* Historical(void* p,void* member){auto&s=*static_cast<ReaderState*>(p);++s.method_reads;return member==s.method?s.historical:Owner(p,member);}
struct Origin{void* array=nullptr;size_t index=0;};
struct Plan:artdcevm::source_commit::Plan{std::vector<Origin> class_root_origins;};
int main(){int current=0,old1=0,old2=0,foreign=0,moved=0;ReaderState state;Set aliases({&state,Decode,nullptr,Owner,nullptr});
 Weak weak1{&current},weak2{&current};Old first{&old1},second{&old2};
 assert(aliases.Equivalent(&current,&current)&&!aliases.Equivalent(&current,&old1));
 auto node=std::make_unique<Node>();node->aliases={{&weak1,&first},{&weak2,&second}};aliases.Publish(std::move(node));
 assert(aliases.Equivalent(&current,&old1)&&aliases.Equivalent(&current,&old2));
 int reads=state.metadata_reads;assert(!aliases.Equivalent(&foreign,&old1)&&state.metadata_reads==reads);
 weak1.current=nullptr;reads=state.metadata_reads;assert(!aliases.Equivalent(&current,&old1));
 // A cleared weak never dereferences the former native old-field allocation.
 assert(state.metadata_reads==reads+1); // only the other still-live alias is read
 assert(aliases.Deactivate(&weak1)&&!aliases.Deactivate(&weak1));
 weak2.current=&moved;assert(aliases.Equivalent(&moved,&old2)&&!aliases.Equivalent(&current,&old2));
 weak2.readable=false;reads=state.metadata_reads;assert(!aliases.Equivalent(&moved,&old2)&&state.metadata_reads==reads);
 assert(aliases.WeakHandles().size()==1);assert(aliases.Deactivate(&weak2)&&aliases.WeakHandles().empty());
 ReaderState moving;int actual_thread=0,field=0,method=0;moving.current=&current;moving.historical=&old1;moving.moved_current=&moved;moving.moved_historical=&old2;moving.thread=&actual_thread;moving.suspend=true;
 moving.field=&field;moving.method=&method;
 Set refreshed({&moving,nullptr,DecodeThread,Current,Historical});moving.aliases=&refreshed;Weak moving_weak{&current};Old anchor{&old2};
 auto moved_node=std::make_unique<Node>();moved_node->aliases={{&moving_weak,&anchor}};refreshed.Publish(std::move(moved_node));
 assert(refreshed.EquivalentWriter(&actual_thread,&field,&method)); // roots reread AFTER simulated suspension
 std::promise<void> started,proceed;moving.started=&started;moving.proceed=proceed.get_future().share();moving.suspend=false;
 auto waiting=std::async(std::launch::async,[&]{return refreshed.EquivalentWriter(&actual_thread,&field,&method);});
 started.get_future().wait();
 auto additional=std::make_unique<Node>();refreshed.Publish(std::move(additional)); // exclusive publish must not wait for suspended weak read
 auto retiring=std::async(std::launch::async,[&]{return refreshed.Deactivate(&moving_weak);});
 assert(retiring.wait_for(std::chrono::milliseconds(10))==std::future_status::timeout);
 proceed.set_value();assert(waiting.get()&&retiring.get());assert(refreshed.WeakHandles().empty());
 // A previously fieldless Class has an old ArtMethod anchor, not a fake
 // ArtField. Both kinds use the same weak-before-metadata lease and retirement.
 ReaderState method_state;method_state.thread=&actual_thread;method_state.field=&field;method_state.method=&method;
 method_state.current=&current;method_state.historical=&old1;
 Set method_aliases({&method_state,nullptr,DecodeThread,Current,Historical});method_state.aliases=&method_aliases;
 Weak method_weak{&current};Old method_anchor{&old1};
 auto method_node=std::make_unique<Node>();method_node->aliases={{&method_weak,&method_anchor,nullptr,0,MemberKind::Method}};
 method_aliases.Publish(std::move(method_node));
 assert(method_aliases.EquivalentWriter(&actual_thread,&field,&method)&&method_state.method_reads==3&&method_state.metadata_reads==1);
 method_state.moved_current=&moved;method_state.moved_historical=&old2;method_state.suspend=true;method_anchor.owner=&old2;
 assert(method_aliases.EquivalentWriter(&actual_thread,&field,&method)); // Both roots read anew after weak suspension.
 method_state.suspend=false;method_weak.current=nullptr;reads=method_state.metadata_reads;
 assert(!method_aliases.EquivalentWriter(&actual_thread,&field,&method)&&method_state.metadata_reads==reads);
 method_weak.current=&moved;method_anchor.owner=&foreign;
 assert(!method_aliases.EquivalentWriter(&actual_thread,&field,&method));method_anchor.owner=&old2;
 assert(method_aliases.Deactivate(&method_weak)&&method_aliases.WeakHandles().empty());
 // Unknown tags and empty anchors cannot call either native metadata reader.
 Weak invalid_weak{&moved};auto invalid=std::make_unique<Node>();
 invalid->aliases={{&invalid_weak,reinterpret_cast<void*>(1),nullptr,0,static_cast<MemberKind>(255)},
   {&invalid_weak,nullptr,nullptr,0,MemberKind::Method}};method_aliases.Publish(std::move(invalid));
 reads=method_state.metadata_reads;assert(!method_aliases.EquivalentWriter(&actual_thread,&field,&method)&&method_state.metadata_reads==reads);
 assert(method_aliases.Deactivate(&invalid_weak)&&method_aliases.WeakHandles().empty());
 Plan expected;expected.mirror=&old1;expected.new_classes_root=&current;expected.old_classes={&old1};expected.new_classes={&current};expected.old_instances={&old2};expected.new_instances={&moved};expected.class_root_origins={{&current,2}};
 assert(artdcevm::source_commit::SameIdentity(expected,expected));
 auto reject=[&](auto change){Plan actual=expected;change(actual);assert(!artdcevm::source_commit::SameIdentity(expected,actual));};
 reject([&](auto& p){p.mirror=&foreign;});reject([&](auto& p){p.new_classes_root=&foreign;});
 reject([&](auto& p){p.old_classes[0]=&foreign;});reject([&](auto& p){p.new_classes[0]=&foreign;});
 reject([&](auto& p){p.old_instances[0]=&foreign;});reject([&](auto& p){p.new_instances[0]=&foreign;});
 reject([&](auto& p){p.abstract_instances=true;});reject([&](auto& p){p.class_root_origins.clear();});
 reject([&](auto& p){p.class_root_origins[0].array=&foreign;});reject([&](auto& p){++p.class_root_origins[0].index;});
 std::cout<<"Final-field aliases: live weak before native metadata, historical owners, moved/cleared roots, retirement and exact Plan origins passed\n";
}
