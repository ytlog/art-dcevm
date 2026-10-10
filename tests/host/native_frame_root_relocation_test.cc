#define FRAME_RELOCATION_TEST_ONLY
#include "native_runtime_root_provenance_test.cc"
#undef FRAME_RELOCATION_TEST_ONLY
#include "platform/common/native_frame_root_relocation.h"
#include "platform/abi/arm64/stack_visitor.h"
namespace {
using namespace native_frame_root_relocation;
struct Registers:abi::arm64::context::Prefix{
 uintptr_t spill=17;void Reset()override{}void FillCalleeSaves(uint8_t*,const abi::arm64::context::FrameInfo&)override{}
 void SetSP(uintptr_t)override{}void SetPC(uintptr_t)override{}void SetArg0(uintptr_t)override{}
 bool IsAccessibleGPR(uint32_t r)override{return r==0;}uintptr_t*GetGPRAddress(uint32_t r)override{return r==0?&spill:nullptr;}
 uintptr_t GetGPR(uint32_t r)override{assert(r==0);return spill;}void SetGPR(uint32_t r,uintptr_t v)override{assert(r==0);spill=v;}
 bool IsAccessibleFPR(uint32_t)override{return false;}uintptr_t GetFPR(uint32_t)override{return 0;}
};
struct OriginalVisitor:abi::arm64::stack::Visitor13{bool VisitFrame()override{return true;}};
OriginalVisitor original;Registers registers,foreign_registers;
alignas(void*) unsigned char physical[32];int method,header;int stream_mode=0;
void FrameRoots(void*p,Visitor*v,uint8_t flags){assert(p==&runtime_object&&exclusive&&gc&&flags==kAll);if(stream_mode==4)return;
 JavaFrameInfo decl{{nullptr,kJavaFrameRoot,19},&original,kDeclaringClassVreg};void* klass=&header;void**one[]{&klass};v->VisitRoots(one,1,decl.base);
 // Source special roots are distinct from physical compiled GC-mask roots.
 for(size_t special:{kProxyReferenceArgumentVreg,kNativeReferenceArgumentVreg}){JavaFrameInfo info{{nullptr,kJavaFrameRoot,19},&original,special};v->VisitRoots(one,1,info.base);}
 if(stream_mode==5)return;
 uint32_t compressed_root=0;std::memcpy(&compressed_root,physical+16,4);void*temp=reinterpret_cast<void*>(uintptr_t(compressed_root));
 JavaFrameInfo info{{nullptr,kJavaFrameRoot,19},&original,kImpreciseVreg};void**stack[]{&temp};void**gpr[]{reinterpret_cast<void**>(&registers.spill)};
 if(stream_mode==1){if(registers.spill)v->VisitRoots(gpr,1,info.base);if(temp)v->VisitRoots(stack,1,info.base);return;}
 if(temp&&stream_mode!=2)v->VisitRoots(stack,1,info.base);
 // Exact original stack source writes a moved temporary back to compressed storage.
 if(reinterpret_cast<uintptr_t>(temp)!=compressed_root){auto value=uint32_t(reinterpret_cast<uintptr_t>(temp));std::memcpy(physical+16,&value,4);}
 if(registers.spill)v->VisitRoots(gpr,1,info.base);
}
class FrameHooks:public RootHooks{void*FindSymbol(const char*l,const char*n)override{return !std::strcmp(n,kVisit)?Fn(FrameRoots):RootHooks::FindSymbol(l,n);}};
struct MoveTemporary:RootSink{bool Root(const RootRecord&r)noexcept override{
 if(r.type==kJavaFrameRoot&&r.vreg==kImpreciseVreg&&r.slot!=&registers.spill){assert(r.value==reinterpret_cast<void*>(16));*static_cast<void**>(const_cast<void*>(r.slot))=reinterpret_cast<void*>(18);}return true;
}};
std::vector<void*>plan_roots;
bool AddPlan(void*,void*p,size_t*i){*i=plan_roots.size();plan_roots.push_back(p);return true;}
void*GetPlan(void*,size_t i)noexcept{return plan_roots.at(i);}
void SetRoot(uint32_t v){std::memcpy(physical+16,&v,4);}
}
int main(){FrameHooks hooks;std::string error;assert(native_source_stop::Install(hooks,&error));native_source_stop::Service service;
 assert(native_source_stop::Service::Bind(hooks,&heap,State(hooks),&service,&error));Calibrate(service);
 RootProvenance provenance;assert(provenance.Bind(hooks,&runtime_object,Current,&error));
 original.thread=&self;original.quick_frame=reinterpret_cast<void**>(physical);original.header=&header;original.pc=37;original.context=&registers;
 void*method_pointer=&method;std::memcpy(physical,&method_pointer,8);uintptr_t lr=73;std::memcpy(physical+24,&lr,8);
 Frame frame{&self,0,&method,reinterpret_cast<void**>(physical),&header,37,&registers,32,code_info_view::SourceFamily::kAndroid13};
 uint32_t map[8]{};map[3]=1;map[4]=1;uint32_t reg[]{2,1};unsigned char stack=16;
 code_info_view::View view;auto&t=view.tables[0];t.region={reinterpret_cast<const uint8_t*>(map),0,256};t.rows=1;t.row_bits=256;t.column_count=8;for(size_t i=0;i<9;++i)t.columns[i]=i*32;
 auto&r=view.tables[1];r.region={reinterpret_cast<const uint8_t*>(reg),0,64};r.rows=1;r.row_bits=64;r.column_count=2;r.columns[1]=32;r.columns[2]=64;
 auto&st=view.tables[2];st.region={&stack,0,8};st.rows=1;st.row_bits=8;st.column_count=1;st.columns[1]=8;
 modern_field_metadata::ReadContext context{&runtime_object,Extent};source_commit::Plan plan;
 for(int mode=0;mode<6;++mode){Proof proof;plan_roots.clear();assert(proof.Prepare(plan,1,8,{nullptr,AddPlan}));
  native_source_stop::Session stop(service);assert(stop.Begin(&self,sizeof(self)));
  SetRoot(16);registers.spill=17;stream_mode=mode==1?1:mode==2?2:0;original.context=mode==3?&foreign_registers:&registers;
  assert(proof.Begin(stop,&self,context,plan,{nullptr,GetPlan}));forbid_native_allocation=true;
  assert(proof.CaptureFrame(frame,view,0));auto result=proof.Seal(provenance);
  if(mode==1||mode==2||mode==3)assert(!result.completed);
  else{assert(result.completed&&result.slots==2&&result.callbacks==2);assert(proof.VerifyLocationRoot(frame,physical+16,reinterpret_cast<void*>(16)));
   assert(!proof.VerifyLocationRoot(frame,physical+20,reinterpret_cast<void*>(16)));
   assert(!proof.VerifyBeforeReplacement(provenance,plan)); // fresh physical rewalk required
  }
  forbid_native_allocation=false;assert(stop.RollbackBeforeSource());
 }
 // Empty/full-null maps are source facts, never fabricated missing callbacks.
 for(int mode=0;mode<4;++mode){Proof proof;assert(proof.Prepare(plan,1,mode<2?0:8,{nullptr,AddPlan}));native_source_stop::Session stop(service);assert(stop.Begin(&self,sizeof(self)));
  SetRoot(0);registers.spill=0;map[3]=mode>=2?1:0;map[4]=0;stream_mode=mode==0||mode==3?4:5;
  assert(proof.Begin(stop,&self,context,plan,{nullptr,GetPlan})&&proof.CaptureFrame(frame,view,0));auto result=proof.Seal(provenance);
  if(mode==3)assert(!result.completed);else assert(result.completed&&result.callbacks==0); // unseen null GPR still needs independent real spill proof
  assert(stop.RollbackBeforeSource());
 }
 map[3]=map[4]=1;
 // A fresh owner proves complete physical recapture and detects changed GC mask / PC / LR.
 for(int mode=0;mode<4;++mode){Proof proof;assert(proof.Prepare(plan,1,8,{nullptr,AddPlan}));native_source_stop::Session stop(service);assert(stop.Begin(&self,sizeof(self)));
  stream_mode=0;original.context=&registers;SetRoot(16);registers.spill=17;
  assert(proof.Begin(stop,&self,context,plan,{nullptr,GetPlan})&&proof.CaptureFrame(frame,view,0)&&proof.Seal(provenance).completed);
  assert(proof.BeginRecapture(plan,false));Frame changed=frame;if(mode==1)changed.pc++;if(mode==2)stack=32;if(mode==3){uintptr_t bad=74;std::memcpy(physical+24,&bad,8);}
  bool accepted=proof.CaptureFrame(changed,view,0);if(!mode)assert(accepted&&proof.EndRecapture()&&proof.VerifyBeforeReplacement(provenance,plan));else assert(!accepted);
  stack=16;std::memcpy(physical+24,&lr,8);assert(stop.RollbackBeforeSource());
 }
 {native_source_stop::Session stop(service);assert(stop.Begin(&self,sizeof(self)));SetRoot(16);registers.spill=17;stream_mode=0;original.context=&registers;
  MoveTemporary move;auto observed=provenance.ObserveFrameRoots(stop,&self,context,move);assert(observed.completed);
  uint32_t moved=0;std::memcpy(&moved,physical+16,4);assert(moved==18);uintptr_t saved_lr=0;std::memcpy(&saved_lr,physical+24,8);assert(saved_lr==73);
  assert(stop.RollbackBeforeSource());}
 std::cout<<"Actual Session + original Runtime root leaf boundary: nonprecise ordered stack-temp/GPR census, special roots, foreign Context/order/missing refusal and complete physical recapture PASS; no device continuation authority granted\n";
}
