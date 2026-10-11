#include "platform/common/native_call_graph_execution.h"
#include "platform/common/native_interface_dispatch.h"
#include "platform/common/native_interface_graph.h"
#include "platform/common/native_hierarchy_session.h"
#include "platform/common/jvmti_method_flags.h"
#include <algorithm>
#include <map>
#include <set>
#include "platform/common/readable_mapping_scope.h"
#include <unordered_map>

namespace artdcevm::native_call_graph_execution {
bool Union::Prepare(const runnable_jni::Context&context,const native_call_graph_inventory::Inventory&before,
 const native_call_graph_replacement::Batch&replacement,const modern_field_metadata::FieldMetadata&metadata,
 const method_identity::Calibration&calibration,const modern_field_metadata::ReadContext&read,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(attempted_)return deny("call-graph-union-repeated-preparation");attempted_=true;
 if(!before.RootStore()||!replacement.RootStore()||!before.ValidateOrdinary(context,read)||
    !metadata.Calibrated()||!replacement.ClassCount()||!replacement.ValidateOrdinary(read)||!replacement.ValidateSupers(context,read))
  return deny("call-graph-union-rooted-source-inventories-unavailable");
 before_=&before;replacement_=&replacement;metadata_=&metadata;
 layout_=calibration.model==method_identity::Model::kMethod11?method_metadata::kAndroid11:method_metadata::kAndroid12Onward;
 dispatch_reader_=before.DispatchReader();
 if(!PrepareDomain(read,error))return false;
 std::unordered_map<void*,uint64_t>loader_tokens;
 std::unordered_map<void*,size_t>methods;
 const auto append=[&](const auto&inventory){
  const auto*store=inventory.RootStore();
  for(size_t i=0;i<inventory.Nodes().size();++i){
   const auto*method=inventory.MethodAt(i);void*klass=nullptr;
   evolution::MappedDexMetadata dex{};field_copy::CopyClassView named{};
   if(!method||!store->Read(method->declaring_root,&klass)||
      !native_interface_dispatch::ReadCurrentNamespace(metadata,klass,read,&dex,&named)||
      dex.begin!=inventory.Nodes()[i].identity.dex_namespace||
      method->method!=inventory.Nodes()[i].identity.native_method||
      !methods.emplace(method->method,nodes_.size()).second)return false;
   uint64_t token=0;
   if(named.loader){auto found=loader_tokens.find(named.loader);
    if(found==loader_tokens.end()){
     token=loaders_.size()+1;loader_tokens.emplace(named.loader,token);loaders_.push_back(nodes_.size());
    }else token=found->second;
   }
   auto node=inventory.Nodes()[i];node.identity.loader_token=token;
   rooted_.push_back({store,store->Generation(),*method});nodes_.push_back(node);
  }
  return true;
 };
 if(!append(before)||!append(replacement))return deny("call-graph-union-native-identity-or-loader-join-refused");
 const size_t offset=before.Nodes().size();
 std::unordered_map<void*,size_t>class_indices,declarations;
 for(size_t i=0;i<classes_.size();++i){void*klass=nullptr;
  if(!classes_[i].store->Read(classes_[i].root,&klass)||!class_indices.emplace(klass,i).second)
   return deny("call-graph-union-current-class-index-refused");
 }
 for(size_t i=0;i<declarations_.size();++i)
  if(!declarations.emplace(declarations_[i].method.method,i).second)
   return deny("call-graph-union-declaration-carrier-ambiguous");
 const auto emit=[&](native_call_graph::Edge edge,const native_call_graph_resolution::Result*receipt){
  const auto unresolved=[&]{edge.target=SIZE_MAX;edge.coverage=native_call_graph::Coverage::Unresolved;edges_.push_back(edge);};
  if(!receipt||receipt->status!=native_call_graph_resolution::Status::Resolved){unresolved();return true;}
  const auto&r=*receipt;
  if(edge.caller>=nodes_.size()||r.caller!=nodes_[edge.caller].identity.native_method||
     r.dex_namespace!=nodes_[edge.caller].identity.dex_namespace||r.dex_pc!=edge.dex_pc||
     native_call_graph::Kind(r.instruction.kind)!=edge.kind)return false;
  auto declaration=declarations.find(r.declaration);
  if(declaration==declarations.end()){unresolved();return true;}
  if(edge.kind==native_call_graph::InvokeKind::Direct||edge.kind==native_call_graph::InvokeKind::Static){
   auto target=methods.find(r.declaration);
   if(target==methods.end()){unresolved();return true;}
   const auto&method=rooted_[target->second].method;
   if((method.modifiers&0x400u)||bool(method.modifiers&8u)!=(edge.kind==native_call_graph::InvokeKind::Static)){
    unresolved();return true;
   }
   edge.target=target->second;edge.coverage=native_call_graph::Coverage::Exact;edges_.push_back(edge);return true;
  }
  if((edge.kind!=native_call_graph::InvokeKind::Virtual&&edge.kind!=native_call_graph::InvokeKind::Interface)||!dispatch_reader_){
   unresolved();return true;
  }
  const auto&bound=declarations_[declaration->second];void*declaring=nullptr;
  if(!bound.store->Read(bound.method.declaring_root,&declaring))return false;
  auto owner=class_indices.find(declaring);if(owner==class_indices.end())return false;
  std::vector<size_t>targets;std::vector<Dispatch>witnesses;
  for(size_t receiver=0;receiver<classes_.size();++receiver){const auto&klass=classes_[receiver];
   if(klass.flags&0x600u)continue;
   if(std::find(klass.closure.begin(),klass.closure.end(),owner->second)==klass.closure.end())continue;
   void*current=nullptr;native_call_graph_dispatch::Observation observed{};
   if(!klass.store->Read(klass.root,&current))return false;
   if(!dispatch_reader_->Observe(edge.kind,r.declaration,current,read,&observed)||
      observed.declaration!=r.declaration||observed.declaring_class!=declaring||observed.receiver_class!=current){
    unresolved();return true;
   }
   auto target=methods.find(observed.target);
   if(target==methods.end()){unresolved();return true;}
   targets.push_back(target->second);witnesses.push_back({edge.kind,r.declaration,observed.target,owner->second,receiver});
  }
  if(targets.empty()){unresolved();return true;}
  std::sort(targets.begin(),targets.end());targets.erase(std::unique(targets.begin(),targets.end()),targets.end());
  dispatches_.insert(dispatches_.end(),witnesses.begin(),witnesses.end());
  for(size_t target:targets){edge.target=target;edge.coverage=native_call_graph::Coverage::ClosedDispatch;edges_.push_back(edge);}
  return true;
 };
 using CallKey=std::pair<void*,uint32_t>;
 std::map<CallKey,const native_call_graph_resolution::Result*>before_receipts;
 for(const auto&r:before.Resolutions())if(r.status==native_call_graph_resolution::Status::Resolved)
  if(!before_receipts.emplace(CallKey{r.caller,r.dex_pc},&r).second)
   return deny("call-graph-union-before-original-callsite-ambiguous");
 std::set<std::pair<size_t,uint32_t>>calls;
 for(const auto&edge:before.Edges()){
  if(edge.caller>=before.Nodes().size())return deny("call-graph-union-before-caller-outside-inventory");
  if(!calls.emplace(edge.caller,edge.dex_pc).second)continue;
  if(edge.kind==native_call_graph::InvokeKind::Super&&edge.coverage==native_call_graph::Coverage::Exact){
   // The old caller's separate rooted super receipt is still validated by the
   // BEFORE inventory. Incoming super callers need their own source receipt.
   edges_.push_back(edge);continue;
  }
  auto receipt=before_receipts.find({before.Nodes()[edge.caller].identity.native_method,edge.dex_pc});
  if(!emit(edge,receipt==before_receipts.end()?nullptr:receipt->second))
   return deny("call-graph-union-before-original-callsite-receipt-differs");
 }
 const auto&receipts=replacement.Resolutions();
 if(replacement.CallsResolved()&&receipts.size()!=replacement.Edges().size())
  return deny("call-graph-union-original-resolution-coverage-differs");
 for(size_t i=0;i<replacement.Edges().size();++i){
  auto edge=replacement.Edges()[i];edge.caller+=offset;
  if(edge.kind==native_call_graph::InvokeKind::Super){
   void*target=replacement.SuperTarget(nodes_[edge.caller].identity.native_method,edge.dex_pc);
   auto found=methods.find(target);
   if(target&&found!=methods.end()){
    edge.target=found->second;edge.coverage=native_call_graph::Coverage::Exact;edges_.push_back(edge);continue;
   }
  }
  if(!emit(edge,replacement.CallsResolved()?&receipts[i]:nullptr))
   return deny("call-graph-union-replacement-original-callsite-receipt-differs");
 }
 std::vector<size_t>seeds=before.Seeds();
 for(size_t i=offset;i<nodes_.size();++i)
  if(nodes_[i].kind==native_call_graph::MethodKind::Managed&&!(rooted_[i].method.modifiers&0x400u))seeds.push_back(i);
 plan_=native_call_graph::Build(nodes_,edges_,seeds);
 if(plan_.failure!=native_call_graph::Failure::None)return deny("call-graph-union-graph-invalid");
 for(size_t i:plan_.interpret){
  if(rooted_[i].method.modifiers&0x400u)return deny("call-graph-union-abstract-interpreter-target");
  (i<offset?before_selection_:replacement_selection_).push_back(rooted_[i].method);
 }
 if(!Check(read))return deny("call-graph-union-fresh-rooted-identities-refused");
 prepared_=true;if(error)error->clear();return true;
}
bool Union::PrepareDomain(const modern_field_metadata::ReadContext&read,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 const auto&class_layout=metadata_->StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 std::unordered_map<void*,size_t>indices;
 const auto append=[&](const auto&inventory){const auto*store=inventory.RootStore();
  for(const auto&source:inventory.Classes()){
   void*klass=nullptr;size_t extent=0;class_metadata::State state{};
   if(!store->Read(source.root,&klass)||!klass||!indices.emplace(klass,classes_.size()).second||
      !read.extent(read.owner,klass,&extent)||!class_metadata::ReadState(class_layout,klass,extent,&state)||
      (state.access_flags&0x600u)!=(source.access&0x600u))return false;
   RootedClass root;root.store=store;root.generation=store->Generation();root.root=source.root;
   root.parent=source.parent;root.interfaces=source.interfaces;root.flags=state.access_flags;
   classes_.push_back(std::move(root));
  }
  for(const auto&method:inventory.Declarations())declarations_.push_back({store,store->Generation(),method});
  return true;
 };
 if(!append(*before_)||!append(*replacement_))return deny("call-graph-union-complete-rooted-class-domain-refused");
 std::vector<std::vector<size_t>>ancestors(classes_.size());
 for(size_t i=0;i<classes_.size();++i){auto&root=classes_[i];void*klass=nullptr,*parent=nullptr;
  if(!root.store->Read(root.root,&klass)||!root.store->Read(root.parent,&parent))return deny("call-graph-union-parent-root-unavailable");
  void*actual=metadata_->Reference(klass,offsetof(class_metadata::Class11,super_class));
  if(actual!=parent){
   field_copy::CopyClassView object{};
   if(!(root.flags&0x200u)||parent||!actual||
      !metadata_->ClassIdentityVerified(actual,"Ljava/lang/Object;",nullptr,nullptr,nullptr,&object,nullptr,&read)||object.loader)
    return deny("call-graph-union-native-parent-differs-from-public-root");
  }
  if(actual){auto found=indices.find(actual);if(found==indices.end())return deny("call-graph-union-parent-outside-rooted-census");
   root.native_parent=found->second;ancestors[i].push_back(found->second);
  }
  for(auto slot:root.interfaces){void*interface=nullptr;
   if(!root.store->Read(slot,&interface))return deny("call-graph-union-interface-root-unavailable");
   auto found=indices.find(interface);
   if(found==indices.end()||!(classes_[found->second].flags&0x200u))return deny("call-graph-union-interface-outside-rooted-census");
   if(std::find(ancestors[i].begin(),ancestors[i].end(),found->second)!=ancestors[i].end())
    return deny("call-graph-union-public-ancestor-duplicated");
   ancestors[i].push_back(found->second);
  }
 }
 // Iterative complete public ancestor closure, with a separate cycle check.
 // No native IfTable contributes a missing receiver or invents public ancestry.
 std::vector<uint8_t>colors(classes_.size(),0);
 struct Frame {size_t node,next;};std::vector<Frame>stack;
 for(size_t start=0;start<classes_.size();++start){if(colors[start])continue;
  colors[start]=1;stack.push_back({start,0});
  while(!stack.empty()){auto&frame=stack.back();
   if(frame.next==ancestors[frame.node].size()){colors[frame.node]=2;stack.pop_back();continue;}
   const auto next=ancestors[frame.node][frame.next++];
   if(colors[next]==1)return deny("call-graph-union-public-ancestor-cycle");
   if(!colors[next]){colors[next]=1;stack.push_back({next,0});}
  }
 }
 std::vector<size_t>pending;std::vector<bool>seen(classes_.size(),false);
 for(size_t i=0;i<classes_.size();++i){std::fill(seen.begin(),seen.end(),false);pending.push_back(i);
  while(!pending.empty()){const auto n=pending.back();pending.pop_back();if(seen[n])continue;seen[n]=true;
   classes_[i].closure.push_back(n);pending.insert(pending.end(),ancestors[n].begin(),ancestors[n].end());
  }
 }
 return CheckDomain(read)||deny("call-graph-union-native-interface-closure-differs-from-public-roots");
}
bool Union::CheckDomain(const modern_field_metadata::ReadContext&read)const noexcept{
 if(!metadata_||!read.owner||!read.extent)return false;
 const auto&class_layout=metadata_->StorageModel()==modern_field_metadata::Model::kSplit?class_metadata::kLegacy:class_metadata::k17;
 for(size_t i=0;i<classes_.size();++i){const auto&root=classes_[i];void*klass=nullptr,*parent=nullptr;size_t extent=0;class_metadata::State state{};
  if(!root.store||root.store->Generation()!=root.generation||!root.store->Read(root.root,&klass)||
     !read.extent(read.owner,klass,&extent)||!class_metadata::ReadState(class_layout,klass,extent,&state)||state.access_flags!=root.flags)return false;
  if(root.native_parent!=SIZE_MAX){if(root.native_parent>=classes_.size())return false;const auto&p=classes_[root.native_parent];
   if(!p.store->Read(p.root,&parent))return false;
  }
  if(metadata_->Reference(klass,offsetof(class_metadata::Class11,super_class))!=parent)return false;
  native_interface_graph::View view{};
  if(!native_interface_graph::ReadView(*metadata_,klass,&view,nullptr,&read))return false;
  size_t expected=0;
  for(size_t n:root.closure)if(n!=i&&(classes_[n].flags&0x200u))++expected;
  if(view.Count()!=expected)return false;
  for(size_t n:root.closure){if(n==i||!(classes_[n].flags&0x200u))continue;
   void*wanted=nullptr;const auto&interface=classes_[n];
   if(!interface.store->Read(interface.root,&wanted))return false;
   size_t matches=0;
   for(size_t slot=0;slot<view.Count();++slot){void*actual=nullptr;
    if(!native_interface_graph::InterfaceAt(*metadata_,view,slot,&actual,&read))return false;matches+=actual==wanted;
   }
   if(matches!=1)return false;
  }
 }
 for(const auto&root:declarations_){void*declaring=nullptr,*containing=nullptr;size_t extent=0;method_metadata::Identity identity{};
  if(!root.store||root.store->Generation()!=root.generation||!root.store->Read(root.method.declaring_root,&declaring)||
     !root.store->Read(root.method.containing_root,&containing)||!read.extent(read.owner,root.method.method,&extent)||
     !method_metadata::ReadIdentity(layout_,root.method.method,extent,&identity)||identity.dex_method_index!=root.method.dex_index||
     jvmti_method_flags::Public(identity.access_flags)!=root.method.modifiers||metadata_->MethodDeclaringClass(root.method.method)!=declaring||
     !read.extent(read.owner,containing,&extent)||extent<56)return false;
  uint64_t address=0;std::memcpy(&address,static_cast<const unsigned char*>(containing)+offsetof(class_metadata::Class11,methods),8);
  const auto*array=reinterpret_cast<void*>(uintptr_t(address));
  if(!read.extent(read.owner,array,&extent)||!native_selective_interpretation::detail::Member(array,extent,root.method.method,layout_.size))return false;
 }
 return true;
}
bool Union::Check(const modern_field_metadata::ReadContext&read)const noexcept{
 if(!metadata_||rooted_.size()!=nodes_.size()||!read.owner||!read.extent||!CheckDomain(read))return false;
 const auto loader=[&](size_t i,void**out){
  if(i>=rooted_.size())return false;const auto&root=rooted_[i];void*klass=nullptr;
  evolution::MappedDexMetadata dex{};field_copy::CopyClassView named{};
  if(!root.store||root.store->Generation()!=root.generation||
     !root.store->Read(root.method.declaring_root,&klass)||
     !native_interface_dispatch::ReadCurrentNamespace(*metadata_,klass,read,&dex,&named)||
     dex.begin!=nodes_[i].identity.dex_namespace||root.method.method!=nodes_[i].identity.native_method)return false;
  *out=named.loader;return true;
 };
 for(size_t i=0;i<nodes_.size();++i){void*actual=nullptr;
  if(!loader(i,&actual))return false;
  const auto token=nodes_[i].identity.loader_token;
  if(!token){if(actual)return false;}
  else {void*representative=nullptr;
   if(token>loaders_.size()||!loader(loaders_[token-1],&representative)||!actual||actual!=representative)return false;
  }
 }
 for(const auto&w:dispatches_){void*receiver=nullptr,*declaring=nullptr;native_call_graph_dispatch::Observation actual{};
  if(!dispatch_reader_||w.receiver>=classes_.size()||w.declaring>=classes_.size()||
     !classes_[w.receiver].store->Read(classes_[w.receiver].root,&receiver)||
     !classes_[w.declaring].store->Read(classes_[w.declaring].root,&declaring)||
     !dispatch_reader_->Observe(w.kind,w.declaration,receiver,read,&actual)||actual.declaration!=w.declaration||
     actual.declaring_class!=declaring||actual.receiver_class!=receiver||actual.target!=w.target)return false;
 }
 for(size_t i=0;i<loaders_.size();++i){void*a=nullptr;if(!loader(loaders_[i],&a)||!a)return false;
  for(size_t j=0;j<i;++j){void*b=nullptr;if(!loader(loaders_[j],&b)||a==b)return false;}
 }
 return true;
}
bool Union::Validate(const native_source_stop::Session&stop,
 const modern_field_metadata::ReadContext&read)const noexcept{
 return prepared_&&before_&&replacement_&&before_->ValidateOrdinary(stop,read)&&
  replacement_->Validate(stop,read)&&Check(read);
}
bool Execution::SameGeneration()const noexcept {
 return roots_&&inventory_.RootStore()==roots_&&roots_->Generation()==generation_;
}
bool Execution::RefusePrepare(Status status,const char*failure,std::string*error){
 phase_=Phase::Refused;report_.status=status;report_.failure=failure;
 report_.compiled_drained=false;report_.replacement_bodies_drained=false;
 if(error&&error->empty())*error=failure;return false;
}
Report Execution::Refuse(Status status,const char*failure)noexcept {
 phase_=Phase::Refused;report_.status=status;report_.failure=failure;
 report_.compiled_drained=false;report_.replacement_bodies_drained=false;return report_;
}
bool Execution::Prepare(HookEngine&hooks,JNIEnv*env,jvmtiEnv*ti,
 const std::vector<jclass>&input_roots,const runnable_jni::Context&context,
 native_schema_guard_verifier::Verifier&verifier,receiver_guard::SourceProducer&source,
 const method_identity::Calibration&methods,const modern_field_metadata::FieldMetadata&metadata,
 stack_visitor_service::Declaration declaration,std::string*error,
 const native_call_graph_super::Resolver*super_resolver){
 if(phase_!=Phase::Empty||!env||!ti||context.env!=env||!context.self||
    !runnable_jni::Bridge::BorrowOriginalScope(context)||native_source_class_loading::Run::Current())
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-original-ordinary-operation-unavailable",error);
 phase_=Phase::Preparing;self_=context.self;super_resolver_=super_resolver;
 if(!selection_.CalibrateOwner(hooks,env,error)||!replacement_selection_.CalibrateOwner(hooks,env,error)||!union_selection_.CalibrateOwner(hooks,env,error)||
    !inventory_.Prepare(context,verifier,source,error)||
    !(super_resolver?inventory_.ResolveCalls(context,source,*super_resolver,error):
                     inventory_.ResolveCalls(context,source,error))||
    !verifier.RefreshCallGraphCensus(env,ti,input_roots,error))
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-real-inventory-resolution-census-refused",error);
 roots_=inventory_.RootStore();generation_=roots_?roots_->Generation():0;
 if(!SameGeneration())return RefusePrepare(Status::PreparationRefused,"call-graph-execution-root-generation-unavailable",error);
 report_.graph_native_boundaries=inventory_.GraphPlan().native_boundaries.size();
 report_.native_boundary_obligations=report_.graph_native_boundaries!=0;
 if(!inventory_.GraphPlan().complete)
  return RefusePrepare(Status::UnsupportedIncompleteGraph,"call-graph-execution-complete-callgraph-unavailable",error);
 const auto&selected=inventory_.Selection();
 if(!selection_.Prepare(hooks,methods,metadata,*roots_,selected.data(),selected.size(),error)||
    !selection_.PrepareRefund(hooks,error)||!stack_.Bind(hooks,declaration,error)||!stack_.Prepare(selection_,&stack_prepared_,error)||
    !stack_.Reserve(&replacement_stack_,error)||!stack_.Reserve(&union_stack_,error)||!SameGeneration())
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-real-selection-stack-preparation-refused",error);
 methods_=&methods;metadata_=&metadata;source_=&source;verifier_=&verifier;
 phase_=Phase::Prepared;report_.status=Status::Prepared;report_.failure=nullptr;return true;
}
bool Execution::PrepareReplacements(HookEngine&hooks,const runnable_jni::Context&context,jvmtiEnv*ti,
 const native_source_class_loading::Run&loading,const void*driver,
 const native_owned_removal::RootedGraph&graph,
 const std::vector<native_call_graph_replacement::Definition>&definitions,
 bounded_managed_roots::Store&roots,std::string*error){
 if(phase_!=Phase::Inspected||!SameGeneration()||context.self!=self_||!methods_||!metadata_||!source_||!verifier_)
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-replacement-order-or-owner-refused",error);
 phase_=Phase::ReplacementPreparing;report_.compiled_drained=false;
 if(!replacements_.Stage(hooks,context,ti,loading,driver,graph,definitions,roots,*metadata_,*methods_,*source_,error,&inventory_))
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-complete-replacement-binding-refused",error);
 // Resolution may fail with an original exception pending. Prepare the
 // complete replacement identity/stack reader first so that refund never
 // depends on successful resolution or a completed union graph.
 const auto&selected=replacements_.Selection();
 if(!selected.empty()&&(!replacement_selection_.Prepare(hooks,*methods_,*metadata_,roots,selected.data(),selected.size(),error)||
                       !replacement_selection_.PrepareRefund(hooks,error)||
                       !stack_.Prepare(replacement_selection_,&replacement_stack_,error)))
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-replacement-selection-stack-refused",error);
 if(!replacements_.ResolveCalls(context,*source_,error,&inventory_,super_resolver_)||
    !verifier_->RefreshCallGraphCensus(context,ti,loading,driver,graph.holder,graph.metadata.count,error))
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-original-replacement-resolution-census-refused",error);
 {
  method_identity::ScopedReadableMappings mappings;
  if(!mappings.Ready())return RefusePrepare(Status::PreparationRefused,"call-graph-execution-union-mappings-unavailable",error);
  modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
  if(!union_.Prepare(context,inventory_,replacements_,*metadata_,*methods_,read,error))
   return RefusePrepare(Status::PreparationRefused,"call-graph-execution-rooted-union-preparation-refused",error);
 }
 for(const auto&method:union_.BeforeSelection()){
  const auto&before=inventory_.Selection();
  if(std::none_of(before.begin(),before.end(),[&](const auto&m){return m.method==method.method;}))union_extra_.push_back(method);
 }
 if(!union_extra_.empty()&&(!union_selection_.Prepare(hooks,*methods_,*metadata_,*roots_,union_extra_.data(),union_extra_.size(),error)||
                         !union_selection_.PrepareRefund(hooks,error)||!stack_.Prepare(union_selection_,&union_stack_,error)))
  return RefusePrepare(Status::PreparationRefused,"call-graph-execution-union-ancestor-selection-stack-refused",error);
 if(!SameGeneration())return RefusePrepare(Status::PreparationRefused,"call-graph-execution-before-roots-changed-during-replacement-stage",error);
 report_.scope=union_.GraphPlan().complete?GraphScope::InstalledUnionSelection:GraphScope::InstalledBeforeAndReplacementBodies;
 report_.replacement_classes=replacements_.ClassCount();
 report_.replacement_calls=replacements_.Edges().size();
 report_.replacement_call_gaps=replacements_.OutgoingPlan().gaps.size();
 report_.replacement_native_boundaries=replacements_.OutgoingPlan().native_boundaries.size();
 report_.union_nodes=union_.Nodes().size();report_.union_calls=union_.Edges().size();
 report_.union_gaps=union_.GraphPlan().gaps.size();report_.union_complete=union_.GraphPlan().complete;
 report_.native_boundary_obligations|=report_.replacement_native_boundaries!=0||!union_.GraphPlan().native_boundaries.empty();
 phase_=Phase::ReplacementPrepared;report_.status=Status::ReplacementPrepared;report_.failure=nullptr;return true;
}
bool Execution::Inspect(void*raw,native_source_stop::Session&stop)noexcept {
 auto&operation=*static_cast<Execution*>(raw);
 const auto deny=[&](const char*failure){operation.report_.failure=failure;return false;};
 const bool replacement=operation.phase_==Phase::ReplacementInspecting;
 if((operation.phase_!=Phase::Inspecting&&!replacement)||!operation.inspection_read_||
    !stop.IsStopped(operation.self_)||!operation.SameGeneration())
  return deny("call-graph-execution-current-inspection-owner-generation-refused");
 operation.inspection_stop_=&stop;
 struct CallbackScope {
  Execution&operation;
  ~CallbackScope(){operation.inspection_stop_=nullptr;}
 }callback_scope{operation};
 const auto&read=*operation.inspection_read_;
 if(!operation.inventory_.ValidateOrdinary(stop,read))
  return deny("call-graph-execution-current-ordinary-inventory-refused");
 if(replacement&&(!operation.replacements_.Validate(stop,read)||!operation.union_.Validate(stop,read)))
  return deny("call-graph-execution-current-replacement-batch-refused");
 native_selective_interpretation::Report retained;
 if(replacement&&!operation.selection_.ValidateRetained(stop,operation.self_,read,&retained))
  return deny(retained.failure?retained.failure:"call-graph-execution-before-retained-selection-refused");
 if(!replacement&&!operation.selection_.Activate(stop,operation.self_,read,&operation.report_.activation))
  return deny(operation.report_.activation.failure?operation.report_.activation.failure:
              "call-graph-execution-real-selective-activation-refused");
 if(replacement&&!operation.replacements_.Selection().empty()&&
    !operation.replacement_selection_.Activate(stop,operation.self_,read,&operation.report_.replacement_activation))
  return deny(operation.report_.replacement_activation.failure?operation.report_.replacement_activation.failure:
              "call-graph-execution-real-replacement-activation-refused");
 if(replacement&&!operation.union_extra_.empty()&&
    !operation.union_selection_.Activate(stop,operation.self_,read,&operation.report_.union_activation))
  return deny(operation.report_.union_activation.failure?operation.report_.union_activation.failure:
              "call-graph-execution-real-union-ancestor-activation-refused");
 auto&before=replacement?operation.report_.before_reinspected:operation.report_.inspected;
 before=operation.stack_.Scan(stop,operation.self_,operation.threads_,operation.stack_prepared_,read);
 if(!before.walk_complete)
  return deny(before.failure?before.failure:
              "call-graph-execution-complete-inspection-walk-refused");
 if(replacement&&!operation.replacements_.Selection().empty()){
  auto&after=operation.report_.replacement_inspected;
  after=operation.stack_.Scan(stop,operation.self_,operation.threads_,operation.replacement_stack_,read);
  if(!after.walk_complete)return deny(after.failure?after.failure:"call-graph-execution-complete-replacement-walk-refused");
 }
 if(replacement&&!operation.union_extra_.empty()){
  operation.report_.union_inspected=operation.stack_.Scan(stop,operation.self_,operation.threads_,operation.union_stack_,read);
  if(!operation.report_.union_inspected.walk_complete)return deny(operation.report_.union_inspected.failure?
   operation.report_.union_inspected.failure:"call-graph-execution-complete-union-ancestor-walk-refused");
 }
 if(replacement&&(!operation.selection_.ValidateRetained(stop,operation.self_,read,&retained)||
    (!operation.replacements_.Selection().empty()&&
     !operation.replacement_selection_.ValidateRetained(stop,operation.self_,read,&retained))||
    (!operation.union_extra_.empty()&&!operation.union_selection_.ValidateRetained(stop,operation.self_,read,&retained))))
  return deny(retained.failure?retained.failure:"call-graph-execution-postinspection-retained-selections-refused");
 if(!operation.SameGeneration()||!operation.inventory_.ValidateOrdinary(stop,read)||
    (replacement&&(!operation.replacements_.Validate(stop,read)||!operation.union_.Validate(stop,read)))||
    !stop.IsStopped(operation.self_))
  return deny("call-graph-execution-postinspection-inventory-owner-changed");
 return true;
}
Report Execution::InspectActivation(size_t self_extent,
 const modern_field_metadata::ReadContext&read)noexcept {
 const bool replacement=phase_==Phase::ReplacementPrepared;
 if((phase_!=Phase::Prepared&&!replacement)||!SameGeneration()||!read.owner||!read.extent||!self_extent)
  return Refuse(Status::InspectionRefused,"call-graph-execution-inspection-order-or-read-generation-refused");
 phase_=replacement?Phase::ReplacementInspecting:Phase::Inspecting;
 inspection_read_=&read;bool inspected=false,cleanup_complete=false;
 {
  native_source_stop::Session stop(stop_service_);
  inspected=stop.Inspect(self_,self_extent,this,Inspect);
  cleanup_complete=!stop.Reserved()&&!stop.IsStopped(self_);
  if(!inspected&&!report_.failure)report_.failure=stop.Failure();
 }
 // The stack owner is destroyed before returning, including failed inspection.
 inspection_read_=nullptr;
 if(!inspected||!cleanup_complete||!SameGeneration())
  return Refuse(Status::InspectionRefused,report_.failure?report_.failure:
                "call-graph-execution-inspection-resume-or-generation-refused");
 const auto&before=replacement?report_.before_reinspected:report_.inspected;
 const auto&after=report_.replacement_inspected;
 report_.native_boundary_obligations|=before.boundary_owners_required||after.boundary_owners_required||report_.union_inspected.boundary_owners_required;
 if(before.drain_required||!before.selected_drained||
    (replacement&&!replacements_.Selection().empty()&&(after.drain_required||!after.selected_drained))||
    (replacement&&!union_extra_.empty()&&(report_.union_inspected.drain_required||!report_.union_inspected.selected_drained)))
  return Refuse(Status::UnsupportedActiveCompiled,
                "call-graph-execution-active-selected-compiled-frame-cannot-be-promised-to-drain");
 phase_=replacement?Phase::ReplacementInspected:Phase::Inspected;
 report_.status=replacement?Status::ReplacementInspectedDrained:Status::InspectedDrained;
 report_.replacement_bodies_drained=replacement;
 // Graph completeness and body drain remain independently visible. Final
 // permission additionally requires the original collected/frozen source proof.
 report_.compiled_drained=!replacement||union_.GraphPlan().complete;report_.failure=nullptr;return report_;
}
Report Execution::ValidateFinal(const native_source_stop::Session&stop,
 const native_source_class_loading::Run&loading,const void*driver,
 const modern_field_metadata::ReadContext&read)noexcept {
 return ValidateWindow(stop,loading,driver,read,true);
}
Report Execution::ValidateCollected(const native_source_stop::Session&stop,
 const native_source_class_loading::Run&loading,const void*driver,const modern_field_metadata::ReadContext&read)noexcept{
 return ValidateWindow(stop,loading,driver,read,false);
}
Report Execution::ValidateWindow(const native_source_stop::Session&stop,
 const native_source_class_loading::Run&loading,const void*driver,const modern_field_metadata::ReadContext&read,bool final)noexcept{
 const bool collected=phase_==Phase::CollectedReady;
 const bool replacement=phase_==Phase::ReplacementInspected||(collected&&report_.scope!=GraphScope::InstalledBeforeOnly);
 if(replacement&&!union_.GraphPlan().complete)
  return Refuse(Status::UnsupportedIncompleteGraph,"call-graph-execution-replacement-union-resolution-closure-unavailable");
 if((final?(!collected||final_stop_!=&stop||final_read_!=&read||!stop.IsFinalWindow(self_)):
            ((phase_!=Phase::Inspected&&!replacement)||!stop.IsCollectedStop(self_)))||!SameGeneration()||!stop.IsStopped(self_)||
    !loading.Paused()||!driver||!loading.OwnsDriver(driver)||!read.owner||!read.extent)
  return Refuse(Status::FinalRefused,"call-graph-execution-final-source-owner-order-generation-refused");
 if((!collected&&!inventory_.SealLoading(loading,driver,read,stop))||!inventory_.Validate(read,stop))
  return Refuse(Status::FinalRefused,"call-graph-execution-final-source-census-inventory-refused");
 native_selective_interpretation::Report retained;
 if(!selection_.ValidateRetained(stop,self_,read,&retained))
  return Refuse(Status::FinalRefused,retained.failure?retained.failure:
                "call-graph-execution-final-retained-selection-refused");
 if(replacement&&(!union_.Validate(stop,read)||
    (!replacements_.Selection().empty()&&!replacement_selection_.ValidateRetained(stop,self_,read,&retained))||
    (!union_extra_.empty()&&!union_selection_.ValidateRetained(stop,self_,read,&retained))))
  return Refuse(Status::FinalRefused,retained.failure?retained.failure:"call-graph-execution-final-rooted-union-retained-selections-refused");
 report_.final=stack_.Scan(stop,self_,threads_,stack_prepared_,read);
 report_.native_boundary_obligations|=report_.final.boundary_owners_required;
 if(!report_.final.walk_complete||!SameGeneration()||!inventory_.Validate(read,stop)||!stop.IsStopped(self_))
  return Refuse(Status::FinalRefused,report_.final.failure?report_.final.failure:
                "call-graph-execution-final-complete-walk-inventory-refused");
 if(replacement){
  if(!replacements_.Selection().empty())report_.replacement_final=stack_.Scan(stop,self_,threads_,replacement_stack_,read);
  else report_.replacement_final.walk_complete=report_.replacement_final.selected_drained=true;
  if(!union_extra_.empty())report_.union_final=stack_.Scan(stop,self_,threads_,union_stack_,read);
  else report_.union_final.walk_complete=report_.union_final.selected_drained=true;
  report_.native_boundary_obligations|=report_.replacement_final.boundary_owners_required||report_.union_final.boundary_owners_required;
  if(!report_.replacement_final.walk_complete||!report_.union_final.walk_complete||!union_.Validate(stop,read)||!inventory_.Validate(read,stop))
   return Refuse(Status::FinalRefused,"call-graph-execution-final-complete-union-walk-or-domain-refused");
  if(report_.replacement_final.drain_required||!report_.replacement_final.selected_drained||
     report_.union_final.drain_required||!report_.union_final.selected_drained)
   return Refuse(Status::UnsupportedActiveCompiled,"call-graph-execution-final-union-selected-compiled-frame-active");
 }
 if(report_.final.drain_required||!report_.final.selected_drained)
  return Refuse(Status::UnsupportedActiveCompiled,
                "call-graph-execution-final-selected-compiled-frame-active");
 final_stop_=&stop;final_read_=&read;phase_=final?Phase::Finalized:Phase::CollectedReady;report_.status=final?Status::FinalDrained:Status::CollectedDrained;
 report_.compiled_drained=true;report_.failure=nullptr;return report_;
}
bool Execution::ValidateFinalProof(const native_source_stop::Session&stop,
 const modern_field_metadata::ReadContext&read)const noexcept{
 return ValidateWindowProof(stop,read,true);
}
const std::vector<native_call_graph_stack::Frame>*Execution::ValidatedFrames(const native_source_stop::Session&stop,
 const modern_field_metadata::ReadContext&read)const noexcept{
 return ValidateWindowProof(stop,read,phase_==Phase::Finalized)?stack_.Frames(stack_prepared_):nullptr;
}
bool Execution::ValidateCollectedProof(const native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read)const noexcept{
 return ValidateWindowProof(stop,read,false);
}
bool Execution::ValidateWindowProof(const native_source_stop::Session&stop,const modern_field_metadata::ReadContext&read,bool final)const noexcept{
 if((final?phase_!=Phase::Finalized:phase_!=Phase::CollectedReady)||final_stop_!=&stop||final_read_!=&read||
    !(final?stop.IsFinalWindow(self_):stop.IsCollectedStop(self_))||
    !SameGeneration()||!report_.compiled_drained||!inventory_.Validate(read,stop))return false;
 native_selective_interpretation::Report retained;
 if(!selection_.ValidateRetained(stop,self_,read,&retained)||!stack_.ValidateStream(stack_prepared_,stop,self_,read))return false;
 if(report_.scope!=GraphScope::InstalledBeforeOnly){
  if(!union_.GraphPlan().complete||!union_.Validate(stop,read)||
     (!replacements_.Selection().empty()&&!replacement_selection_.ValidateRetained(stop,self_,read,&retained))||
     (!union_extra_.empty()&&!union_selection_.ValidateRetained(stop,self_,read,&retained))||
     (!replacements_.Selection().empty()&&!stack_.ValidateStream(replacement_stack_,stop,self_,read))||
     (!union_extra_.empty()&&!stack_.ValidateStream(union_stack_,stop,self_,read)))return false;
 }
 return final?stop.IsFinalWindow(self_):stop.IsCollectedStop(self_);
}
RefundReport Execution::InspectRefund(const native_source_class_loading::Run&loading,const void*driver,
 size_t self_extent,const modern_field_metadata::ReadContext&read)noexcept{
 return InspectRefundFor(&loading,driver,self_extent,read);
}
RefundReport Execution::InspectRefundInstalled(size_t self_extent,
 const modern_field_metadata::ReadContext&read)noexcept{
 return InspectRefundFor(nullptr,nullptr,self_extent,read);
}
RefundReport Execution::RefundBeforeRun()noexcept{
 return InspectRefundFresh(nullptr,nullptr);
}
RefundReport Execution::InspectRefundFresh(const native_source_class_loading::Run*loading,const void*driver)noexcept{
 RefundReport result;
 if(!CanInspectRefund(loading,driver)){
  result.failure="call-graph-execution-refund-original-thread-owner-refused";return result;
 }
 // Capture in the original Runnable callback, after its preceding source
 // views have closed. The stopped consumer performs no OS lookup or allocation.
 method_identity::ScopedReadableMappings mappings;size_t self_extent=0;
 if(!mappings.Ready()||!mappings.Extent(self_,&self_extent)||!self_extent){
  result.failure="call-graph-execution-refund-fresh-native-mappings-unavailable";return result;
 }
 modern_field_metadata::ReadContext read{&mappings,method_identity::ScopedReadableMappings::ReadExtent};
 return InspectRefundFor(loading,driver,self_extent,read);
}
bool Execution::CanInspectRefund(const native_source_class_loading::Run*loading,const void*driver)const noexcept{
 if(phase_==Phase::Preparing||phase_==Phase::ReplacementPreparing||phase_==Phase::Inspecting||
    phase_==Phase::ReplacementInspecting||phase_==Phase::Finalized||phase_==Phase::CommittedReleased||phase_==Phase::Refunding||
    !SameGeneration()||!source_||!source_->OwnsCurrentThread(self_)||
    !native_source_stop::Session::InspectionAvailable())return false;
 return loading?native_source_class_loading::Run::Current()==loading&&loading->OwnsDriver(driver):
                !native_source_class_loading::Run::Current()&&!replacements_.ClassCount()&&!driver;
}
RefundReport Execution::InspectRefundFor(const native_source_class_loading::Run*loading,const void*driver,
 size_t self_extent,const modern_field_metadata::ReadContext&read)noexcept{
 RefundReport result;
 if(!CanInspectRefund(loading,driver)||!self_extent||!read.owner||!read.extent){
  result.failure="call-graph-execution-refund-inspection-order-or-source-owner-refused";return result;
 }
 phase_=Phase::Refunding;
 struct Request {Execution&operation;const native_source_class_loading::Run*loading;
  const void*driver;const modern_field_metadata::ReadContext&read;RefundReport&result;
 }request{*this,loading,driver,read,result};
 const auto refund=[](void*raw,native_source_stop::Session&stop)noexcept{
  auto&r=*static_cast<Request*>(raw);
  r.operation.inspection_stop_=&stop;r.operation.inspection_read_=&r.read;
  struct EndRead{Execution&operation;~EndRead(){operation.inspection_stop_=nullptr;operation.inspection_read_=nullptr;}}end{r.operation};
  r.result=r.operation.replacements_.ClassCount()?
   r.operation.RefundReplacements(stop,*r.loading,r.driver,r.read):r.operation.RefundInstalled(stop,r.read);
  return r.result.owned_entries_removed&&!r.result.failure;
 };
 bool accepted=false,closed=false;
 {
  native_source_stop::Session stop(stop_service_);
  accepted=stop.Inspect(self_,self_extent,&request,refund);
  closed=!stop.Reserved()&&!stop.IsStopped(self_);
  if(!accepted&&!result.failure)result.failure=stop.Failure();
 }
 phase_=Phase::Refused;report_.status=Status::FinalRefused;report_.compiled_drained=false;
 if(!accepted||!closed||!CanInspectRefund(loading,driver)){
  result.owned_entries_removed=false;result.replacement_entries_released=false;
  if(!result.failure)result.failure="call-graph-execution-refund-inspection-cleanup-or-root-owner-refused";
 }
 return result;
}
RefundReport Execution::ReleaseForCommit(const native_source_stop::Session&stop,
 const modern_field_metadata::ReadContext&read)noexcept{
 RefundReport result;
 if(phase_!=Phase::Finalized||final_stop_!=&stop||final_read_!=&read||
    !SameGeneration()||!stop.IsFinalWindow(self_)||!report_.compiled_drained||
    report_.scope!=GraphScope::InstalledUnionSelection||!CheckInterpretation(read,true)){
  result.failure="call-graph-execution-commit-release-window-refused";return result;
 }
 // Publish the whole rooted union before the first native Undeoptimize.
 // Publication storage was allocated in ordinary Collected preparation;
 // neither this swap nor the original-entry journal allocates our storage.
 if(!interpretation_->Commit(stop,self_,*interpretation_reservation_)){
  result.failure="call-graph-execution-durable-interpretation-publication-refused";return result;
 }
 const auto release=[&](native_selective_interpretation::Service&selection,
                       native_selective_interpretation::RefundReport&entries){
  entries.selected=selection.SelectedCount();entries.pending=selection.PendingRefund();
  if(!entries.pending){entries.complete=true;return true;}
  if(selection.ReleaseCommitted(stop,self_,read,&entries))return true;
  result.failure=entries.failure;return false;
 };
 if(!release(selection_,result.before_entries)||!release(union_selection_,result.union_entries)||
    !release(replacement_selection_,result.entries)||!stop.IsFinalWindow(self_))return result;
 // A successful native erase does not prove that its entry stayed interpreted.
 // Check actual entries after every group has handed off, before heap/root
 // replacement invalidates the rooted inventory used by this operation.
 for(void*method:interpretation_methods_){
  const uint32_t flags=method_metadata::ReadAccessFlags(method);
  if(flags&0x40000u)continue;
  const auto&layout=selection_.MethodLayout();size_t extent=0;
  using namespace abi::arm64::android_common::method_compilation_policy;
  if(!interpretation_->Contains(method)||(flags&(kIntrinsic|kPreCompiled))||!(flags&kDontCompile)||
     !read.extent(read.owner,method,&extent)||extent<layout.entry_point||extent-layout.entry_point<sizeof(uintptr_t)||
     reinterpret_cast<uintptr_t>(method)>UINTPTR_MAX-layout.entry_point||
     (reinterpret_cast<uintptr_t>(method)+layout.entry_point)%alignof(uintptr_t)||
     __atomic_load_n(reinterpret_cast<const uintptr_t*>(static_cast<const unsigned char*>(method)+layout.entry_point),__ATOMIC_RELAXED)!=
      reinterpret_cast<uintptr_t>(interpretation_->interpreter_bridge_)){
   result.failure="call-graph-execution-durable-entry-not-retained-after-native-release";return result;
  }
 }
 result.owned_entries_removed=true;phase_=Phase::CommittedReleased;return result;
}
bool Execution::InterpretationPolicyReady(const native_interpretation_registry::Registry&owner)const noexcept{
 if((phase_!=Phase::ReplacementInspected&&phase_!=Phase::CollectedReady&&phase_!=Phase::Finalized)||
    !SameGeneration()||!report_.union_complete||report_.scope!=GraphScope::InstalledUnionSelection)return false;
 for(const auto*selection:{&selection_,&union_selection_,&replacement_selection_})
  if(selection->SelectedCount()&&!owner.ReadyFor(*selection))return false;
 return true;
}
bool Execution::PrepareInterpretation(native_interpretation_registry::Registry&owner,std::string*error){
 const auto deny=[&](const char*why){if(error)*error=why;return false;};
 if(interpretation_||interpretation_reservation_||phase_!=Phase::ReplacementInspected||!SameGeneration()||
    !native_source_stop::Session::InspectionAvailable()||!report_.union_complete||
    report_.scope!=GraphScope::InstalledUnionSelection)
  return deny("call-graph-execution-durable-preparation-order-refused");
 if(!InterpretationPolicyReady(owner))return deny("call-graph-execution-durable-policy-or-cleanup-owner-unproved");
 std::vector<void*>methods;
 const size_t before=union_.BeforeSelection().size(),replacement=union_.ReplacementSelection().size();
 if(before>methods.max_size()||replacement>methods.max_size()-before)
  return deny("call-graph-execution-durable-carrier-capacity-overflow");
 if(!before&&!replacement)return deny("call-graph-execution-durable-union-has-no-interpreted-carriers");
 methods.reserve(before+replacement);
 for(const auto*selection:{&selection_,&union_selection_,&replacement_selection_})
  for(const auto&method:selection->SelectedMethods())methods.push_back(method.method);
 if(methods.size()!=before+replacement)
  return deny("call-graph-execution-durable-selection-does-not-cover-union");
 for(const auto*selection:{&union_.BeforeSelection(),&union_.ReplacementSelection()})
  for(const auto&method:*selection)if(std::count(methods.begin(),methods.end(),method.method)!=1)
   return deny("call-graph-execution-durable-selection-is-not-union-bijection");
 auto reservation=owner.Prepare(methods,error);if(!reservation)return false;
 interpretation_methods_=std::move(methods);interpretation_reservation_=std::move(reservation);interpretation_=&owner;
 return true;
}
bool Execution::CheckInterpretation(const modern_field_metadata::ReadContext&read,bool obsolete)const noexcept{
 if(!interpretation_||!InterpretationPolicyReady(*interpretation_)||!interpretation_reservation_||
    !interpretation_->PreparedFor(*interpretation_reservation_,interpretation_methods_))return false;
 for(const auto*selection:{&selection_,&union_selection_,&replacement_selection_}){
  if(!selection->SelectedCount())continue;
  for(const auto&method:selection->SelectedMethods())
   if(!selection->Check(method,read,nullptr,obsolete)||!selection->QueryNative(method.method))return false;
 }
 return true;
}
bool Execution::InterpretationPrepared(const modern_field_metadata::ReadContext&read)const noexcept{
 return (phase_==Phase::ReplacementInspected||phase_==Phase::CollectedReady||phase_==Phase::Finalized)&&
  CheckInterpretation(read);
}
bool Execution::RefundOriginalEntries(const native_source_stop::Session&stop,
 const modern_field_metadata::ReadContext&read,RefundReport&result)noexcept{
 if(!SameGeneration()||!stop.IsStopped(self_)){
  result.failure="call-graph-execution-original-refund-root-owner-or-stop-refused";return false;
 }
 // Original Classes are never disposed here. A failed preparation can leave
 // an unprepared selection, but cannot have added an entry without journaling
 // it first. Zero pending entries therefore requires no original removal; it
 // does not prove absence of foreign Instrumentation references.
 const auto refund=[&](native_selective_interpretation::Service&selection,
                       native_selective_interpretation::RefundReport&entries){
  entries.selected=selection.SelectedCount();entries.pending=selection.PendingRefund();
  if(!entries.pending){entries.complete=true;return true;}
  if(selection.Refund(stop,self_,read,&entries))return true;
  result.failure=entries.failure?entries.failure:"call-graph-execution-original-owned-entry-removal-refused";
  return false;
 };
 if(!refund(union_selection_,result.union_entries)||!refund(selection_,result.before_entries))return false;
 if(!stop.IsStopped(self_)){result.failure="call-graph-execution-original-refund-final-stop-changed";return false;}
 // The publication has not happened on any refundable path. Cancel before
 // source cleanup releases the rooted inventories; successful prior registry
 // entries remain owned by their process-wide observer.
 interpretation_reservation_.reset();interpretation_methods_.clear();interpretation_=nullptr;
 return true;
}
RefundReport Execution::RefundInstalled(const native_source_stop::Session&stop,
 const modern_field_metadata::ReadContext&read)noexcept{
 RefundReport result;
 const auto deny=[&](const char*why){result.failure=why;return result;};
 if(phase_==Phase::Preparing||phase_==Phase::Inspecting||phase_==Phase::ReplacementPreparing||
    phase_==Phase::ReplacementInspecting||phase_==Phase::Finalized||phase_==Phase::CommittedReleased||!SameGeneration()||!stop.IsStopped(self_))
  return deny("call-graph-execution-installed-refund-order-or-root-owner-refused");
 // These are original installed Classes. Live shadow frames do not make their
 // metadata disposable, and their original implementation remains available.
 // Restore only the journaled entries; source reclamation is a separate owner.
 if(!RefundOriginalEntries(stop,read,result))return result;
 result.entries=result.before_entries;
 phase_=Phase::Refused;report_.status=Status::FinalRefused;report_.compiled_drained=false;
 report_.failure="call-graph-execution-installed-owned-entries-refunded-no-commit";
 result.owned_entries_removed=true;return result;
}
RefundReport Execution::RefundReplacements(const native_source_stop::Session&stop,
 const native_source_class_loading::Run&loading,const void*driver,
 const modern_field_metadata::ReadContext&read)noexcept{
 RefundReport result;
 result.entries.selected=replacement_selection_.SelectedCount();
 result.entries.pending=replacement_selection_.PendingRefund();
 const auto deny=[&](const char*why){result.failure=why;return result;};
 // Refused operations keep their replacement bindings/journal. Never use the
 // derived readiness flags as a lifetime or cleanup capability.
 if(!replacements_.ClassCount()||phase_==Phase::ReplacementPreparing||
    phase_==Phase::Inspecting||phase_==Phase::ReplacementInspecting||phase_==Phase::Finalized||phase_==Phase::CommittedReleased||
    native_source_class_loading::Run::Current()!=&loading||!loading.OwnsDriver(driver)||
    !stop.IsStopped(self_)||!replacements_.ValidateRefund(stop,read))
  return deny("call-graph-execution-refund-current-replacement-Holder-owner-refused");
 if(!replacements_.Selection().empty()){
  result.inspected=stack_.ScanForRefund(stop,self_,threads_,replacement_stack_,read);
  if(!result.inspected.walk_complete)return deny(result.inspected.failure?result.inspected.failure:
   "call-graph-execution-refund-complete-thread-walk-refused");
  if(result.inspected.selected_quick||result.inspected.selected_shadow)
   return deny("call-graph-execution-refund-replacement-frame-still-retains-method");
  if(!replacements_.ValidateRefund(stop,read)||!replacement_selection_.Refund(stop,self_,read,&result.entries))
   return deny(result.entries.failure?result.entries.failure:"call-graph-execution-refund-owned-entry-removal-refused");
 }else result.entries.complete=true;
 if(!RefundOriginalEntries(stop,read,result))return result;
 // Removing this operation's entries cannot release another Instrumentation
 // owner's native method reference. Preserve metadata and allow a later
 // rooted retry after that owner releases it; never undeoptimize it here.
 if(result.entries.foreign_retained)
  return deny("call-graph-execution-refund-foreign-replacement-entry-still-retains-method");
 if(!stop.IsStopped(self_)||!replacements_.ValidateRefund(stop,read))
  return deny("call-graph-execution-refund-postremoval-Holder-owner-changed");
 phase_=Phase::Refused;report_.compiled_drained=false;report_.replacement_bodies_drained=false;
 report_.status=Status::FinalRefused;
 report_.failure="call-graph-execution-replacement-entries-refunded-no-commit";
 result.owned_entries_removed=true;result.replacement_entries_released=true;return result;
}
bool SourceRefund::Bind(native_hierarchy_session::Bindings&bindings)noexcept{
 if(!bindings.context||bindings.driver!=driver_||bindings.context->self!=execution_.self_||
    execution_.phase_!=Execution::Phase::Inspected||!execution_.SameGeneration()||
    !execution_.source_||!execution_.source_->OwnsCurrentThread(execution_.self_)||
    native_source_class_loading::Run::Current()!=&loading_||!loading_.OwnsDriver(driver_)||
    bindings.refund_owner||bindings.before_private_discard||bindings.refund_complete)return false;
 bindings.refund_owner=this;bindings.before_private_discard=BeforePrivateDiscard;return true;
}
bool SourceRefund::BeforePrivateDiscard(void*owner,const char**refusal)noexcept{
 if(refusal)*refusal=nullptr;
 const auto deny=[&](const char*why){if(refusal)*refusal=why;return false;};
 if(!owner)return deny("call-graph-source-refund-callback-owner-missing");
 auto&adapter=*static_cast<SourceRefund*>(owner);auto&operation=adapter.execution_;
 if(adapter.running_||native_source_class_loading::Run::Current()!=&adapter.loading_||
    !adapter.loading_.OwnsDriver(adapter.driver_)||!operation.source_||
    !operation.source_->OwnsCurrentThread(operation.self_))
  return deny("call-graph-source-refund-current-Run-driver-or-thread-refused");
 adapter.running_=true;
 struct Scope {SourceRefund&owner;~Scope(){owner.running_=false;}}scope{adapter};
 adapter.report_=operation.InspectRefundFresh(&adapter.loading_,adapter.driver_);
 if(adapter.report_.failure||!adapter.report_.owned_entries_removed||
    (operation.replacements_.ClassCount()&&!adapter.report_.replacement_entries_released))
  return deny(adapter.report_.failure?adapter.report_.failure:"call-graph-source-refund-native-entry-release-incomplete");
 if(!operation.source_->OwnsCurrentThread(operation.self_)||
    native_source_class_loading::Run::Current()!=&adapter.loading_||!adapter.loading_.OwnsDriver(adapter.driver_))
  return deny("call-graph-source-refund-postinspection-source-owner-changed");
 return true;
}
} // namespace artdcevm::native_call_graph_execution
