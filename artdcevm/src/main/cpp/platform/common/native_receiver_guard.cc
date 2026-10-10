#include "platform/common/native_receiver_guard.h"
#include "platform/common/native_receiver_frame.h"
#include "platform/common/native_receiver_bytecode.h"
#include <cstring>
#include <cstdlib>
namespace artdcevm::receiver_guard {
namespace {constexpr uint32_t complete=(uint32_t{1}<<static_cast<unsigned>(Path::Count))-1;bool Same(const Invocation&a,const Invocation&b){return a.path==b.path&&a.thread==b.thread&&a.member==b.member&&a.receiver==b.receiver&&a.special_caller==b.special_caller&&a.field==b.field;}}
Guard::Guard(Access access):access_(access){}
bool Guard::BeginProof(const Oracle& oracle)noexcept{
 std::lock_guard<std::mutex> lock(lock_);if(probing_||Active()||!oracle.invocation.thread||!oracle.invocation.member||!oracle.invocation.receiver||!oracle.declaring||!oracle.receiver_class||oracle.invocation.path>=Path::Count)return false;
 proof_ready_.store(false,std::memory_order_release);proof_epoch_.fetch_add(1,std::memory_order_acq_rel);
 (oracle.rejection?refused_:passed_)&=~(uint32_t{1}<<static_cast<unsigned>(oracle.invocation.path));
 oracle_=oracle;probing_=true;seen_=rejected_=false;return true;
}
bool Guard::EndProof(bool succeeded,bool incompatible,bool unchanged)noexcept{
 std::lock_guard<std::mutex> lock(lock_);if(!probing_)return false;
 const bool okay=seen_&&(oracle_.rejection?(rejected_&&!succeeded&&incompatible&&unchanged):(!rejected_&&succeeded&&!incompatible));
 if(okay)(oracle_.rejection?refused_:passed_)|=uint32_t{1}<<static_cast<unsigned>(oracle_.invocation.path);
 probing_=false;oracle_={};proof_ready_.store(passed_==complete&&refused_==complete,std::memory_order_release);return okay;
}
bool Guard::Proven()const noexcept{std::lock_guard<std::mutex> lock(lock_);return !probing_&&passed_==complete&&refused_==complete;}
Guard::ProofWitness Guard::FreezeProof()const noexcept{
 std::lock_guard<std::mutex> lock(lock_);ProofWitness witness;if(!probing_&&passed_==complete&&refused_==complete&&proof_ready_.load(std::memory_order_acquire)){witness.owner_=this;witness.epoch_=proof_epoch_.load(std::memory_order_acquire);}return witness;
}
bool Guard::ValidateProof(const ProofWitness&witness)const noexcept{return witness.owner_==this&&witness.epoch_&&proof_ready_.load(std::memory_order_acquire)&&proof_epoch_.load(std::memory_order_acquire)==witness.epoch_;}
void Guard::Activate(const ProofWitness&witness)noexcept{if(!ValidateProof(witness))std::abort();active_.store(true,std::memory_order_release);}
void Guard::Activate()noexcept{if(!Proven())std::abort();active_.store(true,std::memory_order_release);}
bool Guard::BytecodeProven(const FrameReader&frames)const noexcept{return access_.bytecode&&access_.bytecode->Proven()&&access_.bytecode->UsesFrames(frames);}
bool Guard::CheckBytecode(void*self,void*caller,uint32_t pc)noexcept{
 if(!Interested(Path::BytecodeInvoke,self,nullptr)&&!Interested(Path::BytecodeSpecial,self,nullptr))return false;
 const auto fail=[&](){if(!access_.raise||!self)std::abort();access_.raise(access_.owner,self,"Ljava/lang/IllegalStateException;","Receiver bytecode invocation lost exact source caller/PC");return true;};
 if(!caller||!access_.current||!access_.current(access_.owner,self)||!access_.bytecode)return fail();const auto result=access_.bytecode->Read(self,caller,pc);
 if(result.outcome==BytecodeOutcome::Unavailable)return fail();if(result.outcome==BytecodeOutcome::OriginalException)return true;
 if(result.outcome==BytecodeOutcome::NonInvoke||result.outcome==BytecodeOutcome::GuardedBridge)return false;
 return Check({result.special_caller?Path::BytecodeSpecial:Path::BytecodeInvoke,self,result.member,result.receiver,result.special_caller,nullptr,false});
}
bool Guard::CheckFrame(Path path,void*self,void*member,const void*frame,const InstructionOperands*operands,void*result,bool field,void*special)noexcept{
 if(!Interested(path,self,member))return false;
 const auto fail=[&](){if(result){const uint64_t zero=0;std::memcpy(result,&zero,8);}if(!access_.raise||!self)std::abort();access_.raise(access_.owner,self,"Ljava/lang/IllegalStateException;","Receiver frame lost calibrated source metadata");return true;};
 uint32_t flags=0;void*declaring=nullptr;if(!access_.current||!access_.current(access_.owner,self)||!access_.member||!member||!access_.member(access_.owner,member,field,&flags,&declaring)||!declaring)return fail();
 if(flags&8u)return false;void*receiver=nullptr;if(!access_.frames||!access_.frames->Read(self,frame,operands,&receiver))return fail();return Check({path,self,member,receiver,special,result,field});
}
bool Guard::CheckHandle(void*self,void*object,void*member,bool field,const void*frame,const InstructionOperands*operands,void*result)noexcept{
 if(!Active()&&!Interested(Path::HandleMethod,self,member)&&!Interested(Path::HandleRead,self,member)&&!Interested(Path::HandleWrite,self,member)&&!Interested(Path::HandleSpecial,self,member))return false;
 const auto fail=[&](){if(result){const uint64_t zero=0;std::memcpy(result,&zero,8);}if(!access_.raise||!self)std::abort();access_.raise(access_.owner,self,"Ljava/lang/IllegalStateException;","Receiver MethodHandle kind/caller has no calibrated source identity");return true;};
 uint32_t kind=0;void*caller=nullptr;
 if(!access_.current||!access_.current(access_.owner,self)||!access_.handle||!object||!access_.handle(access_.owner,self,object,&kind,&caller))return fail();
 // Transform/VarHandle bridge kinds are checked at the eventual member entry
 // or actual FieldVarHandle Access; their operand0 belongs to the bridge.
 if(kind>=5&&kind<=7)return field?fail():false;
 uint32_t flags=0;void*declaring=nullptr;
 if(!access_.member||!access_.member(access_.owner,member,field,&flags,&declaring)||!declaring)return fail();
 const bool static_kind=kind==3||kind==10||kind==11;
 if((kind>=8)!=field||static_kind!=bool(flags&8u))return fail();
 if(static_kind)return false;
 const Path path=field?(kind==8?Path::HandleRead:Path::HandleWrite):(kind==1?Path::HandleSpecial:Path::HandleMethod);
 if(field?(kind!=8&&kind!=9):(kind!=0&&kind!=1&&kind!=2&&kind!=4))return fail();
 return CheckFrame(path,self,member,frame,operands,result,field,kind==1?caller:nullptr);
}
bool Guard::InUse()const noexcept{if(Active())return true;std::lock_guard<std::mutex> lock(lock_);return probing_;}
bool Guard::CheckFieldId(Path path,void*self,void*id,void*handle)noexcept{
 if(!Interested(path,self,nullptr))return false;
 if(!access_.current||!access_.current(access_.owner,self)||!access_.field_id||!id){if(!access_.raise||!self)std::abort();access_.raise(access_.owner,self,"Ljava/lang/IllegalStateException;","Receiver JNI field decoder lost owned native access");return true;}
 void*member=nullptr;if(!access_.field_id(access_.owner,self,id,&member)||!member){access_.raise(access_.owner,self,"Ljava/lang/IllegalStateException;","Receiver JNI FieldId has no proven native identity");return true;}
 return CheckReference(path,self,member,handle,nullptr,true);
}
bool Guard::Interested(Path path,void* self,void* member)const noexcept{
 if(Active())return true;std::lock_guard<std::mutex> lock(lock_);return probing_&&oracle_.invocation.path==path&&oracle_.invocation.thread==self&&(!member||oracle_.invocation.member==member);
}
bool Guard::Entry(Path path,void*self,void*member,void*handle,uint32_t compressed,bool encoded,void*result,bool field)noexcept{
 if(!Interested(path,self,member))return false;
 const auto fail=[&](){if(result){const uint64_t zero=0;std::memcpy(result,&zero,8);}if(!access_.raise||!self)std::abort();access_.raise(access_.owner,self,"Ljava/lang/IllegalStateException;","Receiver entry lost calibrated root or member access");return true;};
 uint32_t flags=0;void*declaring=nullptr;
 if(!access_.current||!access_.current(access_.owner,self)||!access_.member||!member||!access_.member(access_.owner,member,field,&flags,&declaring)||!declaring)return fail();
 if(flags&8u||(encoded?!compressed:!handle))return false;
 void*object=nullptr;const bool decoded=encoded?(access_.compressed&&access_.compressed(access_.owner,self,compressed,&object)):(access_.reference&&access_.reference(access_.owner,self,handle,&object));
 if(!decoded||!object)return fail();return Check({path,self,member,object,nullptr,result,field});
}
bool Guard::CheckReference(Path path,void*self,void*member,void*handle,void*result,bool field)noexcept{return Entry(path,self,member,handle,0,false,result,field);}
bool Guard::CheckCompressed(Path path,void*self,void*member,uint32_t reference,void*result,bool field)noexcept{return Entry(path,self,member,nullptr,reference,true,result,field);}
bool Guard::Check(const Invocation& call)noexcept{
 Oracle expected{};bool probe=false;
 {std::lock_guard<std::mutex> lock(lock_);probe=probing_&&Same(call,oracle_.invocation);if(probe)expected=oracle_;}
 if(!Active()&&!probe)return false;
 const auto fail=[&](const char*type,const char*message){if(call.result){const uint64_t zero=0;std::memcpy(call.result,&zero,8);}if(!access_.raise||!call.thread)std::abort();access_.raise(access_.owner,call.thread,type,message);return true;};
 if(call.path>=Path::Count||!access_.current||!access_.member||!access_.object_class||!access_.normalize||!access_.assignable||!access_.raise||!access_.current(access_.owner,call.thread))return fail("Ljava/lang/IllegalStateException;","Receiver guard lost calibrated mutator context");
 uint32_t flags=0;void *declaring=nullptr,*receiver_class=nullptr,*selected=nullptr;
 if(!call.member||!access_.member(access_.owner,call.member,call.field,&flags,&declaring)||!declaring)return fail("Ljava/lang/IllegalStateException;","Receiver guard lost exact native member identity");
 // Preserve static and null receiver semantics. ART retains its own NPE/type
 // handling; a static Class argument must never be decoded as an instance.
 if(flags&8u||!call.receiver)return false;
 if(!access_.object_class(access_.owner,call.receiver,&receiver_class)||!receiver_class||!access_.normalize(access_.owner,call.thread,declaring,receiver_class,&selected)||!selected)return fail("Ljava/lang/IllegalStateException;","Receiver guard lost current Class alias");
 bool related=false;if(!access_.assignable(access_.owner,selected,receiver_class,&related))return fail("Ljava/lang/IllegalStateException;","Receiver guard could not prove native assignability");
 if(related&&(call.path==Path::HandleSpecial||call.path==Path::BytecodeSpecial)){void*caller=nullptr;if(!call.special_caller||!access_.special||!access_.normalize(access_.owner,call.thread,call.special_caller,receiver_class,&caller)||!caller||!access_.special(access_.owner,selected,caller,&related))return fail("Ljava/lang/IllegalStateException;","Special MethodHandle caller relation could not be proved");}
 if(probe){std::lock_guard<std::mutex> lock(lock_);if(probing_&&Same(call,oracle_.invocation)&&declaring==expected.declaring&&receiver_class==expected.receiver_class){seen_=true;rejected_=!related;}}
 if(related)return false;
 return fail("Ljava/lang/IncompatibleClassChangeError;","Receiver lost its member declaring Class relationship");
}
}
