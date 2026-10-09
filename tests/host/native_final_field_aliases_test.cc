#include "platform/common/native_final_field_aliases.h"
#include "platform/common/source_commit_plan_identity.h"
#include "platform/common/source_commit.h"
#include <cassert>
#include <iostream>
using namespace artdcevm::final_field_aliases;
struct Weak {void* current=nullptr;bool readable=true;};struct Old {void* owner=nullptr;};
struct ReaderState {int metadata_reads=0;};
bool Decode(void*,void* weak,void** value){auto& w=*static_cast<Weak*>(weak);*value=w.current;return w.readable;}
void* Owner(void* raw,void* old){++static_cast<ReaderState*>(raw)->metadata_reads;return static_cast<Old*>(old)->owner;}
struct Origin{void* array=nullptr;size_t index=0;};
struct Plan:artdcevm::source_commit::Plan{std::vector<Origin> class_root_origins;};
int main(){int current=0,old1=0,old2=0,foreign=0,moved=0;ReaderState state;Set aliases({&state,Decode,Owner});
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
