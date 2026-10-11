#pragma once
#include "platform/common/native_dex_instruction_width.h"
#include "platform/common/native_boot_aot_source.h"
#include "platform/common/native_managed_stack_chain.h"
namespace artdcevm::native_shadow_frame_source {
class Source;
// Current registered fragment-head code facts only. This owns no interpreter
// continuation, vreg/GC reconstruction, native helper or dispatch permission.
class Receipt final {
 public:
 Receipt()=default;Receipt(const Receipt&)=delete;Receipt&operator=(const Receipt&)=delete;
 Receipt(Receipt&&)=delete;Receipt&operator=(Receipt&&)=delete;
 void*Method()const noexcept{return method_;}uint32_t DexPc()const noexcept{return pc_;}
 const receiver_guard::CodeView&Code()const noexcept{return code_;}
 private:
 friend class Source;
 void Clear()noexcept{source_=nullptr;visitor_=nullptr;extent_=0;shadow_=method_=nullptr;pc_=0;entry_=nullptr;code_={};}
 const Source*source_=nullptr;const void*visitor_=nullptr;size_t extent_=0;
 void*shadow_=nullptr,*method_=nullptr;uint32_t pc_=0;
 const native_boot_method_catalog::Method*entry_=nullptr;receiver_guard::CodeView code_{};
};
class Source final {
 public:
 Source(const stack_visitor_service::Service&stack,const native_managed_stack_chain::Chain&chain,
  const native_boot_aot_unaffected::Source&window,const native_boot_method_catalog::Rooted&catalog,
  const source_handle_roots::StoppedReader&roots,const method_metadata::Layout&layout)
 :stack_(stack),chain_(chain),window_(window),catalog_(catalog),roots_(roots),layout_(layout){}
 Source(const Source&)=delete;Source&operator=(const Source&)=delete;
 bool Observe(const void*visitor,size_t extent,const modern_field_metadata::ReadContext&read,
  Receipt*out,const char**failure=nullptr)const noexcept {
  auto deny=[&](const char*s){if(failure)*failure=s;return false;};if(failure)*failure=nullptr;
  if(!out)return deny("shadow-source-receipt-storage-missing");out->Clear();
  const auto matches=[&](const method_metadata::Layout&x){return layout_.size==x.size&&layout_.dex_method_index==x.dex_method_index&&layout_.data==x.data&&layout_.entry_point==x.entry_point;};
  if(!matches(method_metadata::kAndroid11)&&!matches(method_metadata::kAndroid12Onward))return deny("shadow-source-method-declaration-unavailable");
  size_t mapped=0;if(!read.owner||!read.extent||!read.extent(read.owner,visitor,&mapped)||mapped<extent||!chain_.Validate())return deny("shadow-source-current-registered-chain-unproved");
  void*thread=nullptr,*shadow=nullptr;
  if(!stack_.ShadowHead(visitor,extent,&thread,&shadow)||thread!=chain_.Thread())return deny("shadow-source-current-shadow-cursor-unproved");
  bool member=false;for(size_t i=0;i<chain_.Fragments();++i){const auto*fragment=chain_.At(i);if(fragment&&fragment->shadow==shadow)member=true;}
  if(!member)return deny("shadow-source-current-fragment-head-unproved");
  void*method=nullptr;uint32_t pc=UINT32_MAX;
  if(!stack_.Method(visitor,extent,&method)||!method||!stack_.DexPc(visitor,extent,&pc)||pc==UINT32_MAX)return deny("shadow-source-original-method-or-dex-pc-unavailable");
  const auto*entry=catalog_.Lookup(method,roots_,read);method_metadata::Identity identity{};receiver_guard::CodeView code;
  if(!entry||!read.extent(read.owner,method,&mapped)||!method_metadata::ReadIdentity(layout_,method,mapped,&identity)||
   (identity.access_flags&0xffffu)!=(uint32_t(entry->modifiers)&0xffffu)||
   !window_.CurrentCalleeCode(method,read,&code)||!native_boot_aot_unaffected::ImmutableBodyMatches(*entry,code,identity.dex_method_index,read))return deny("shadow-source-current-rooted-public-CodeItem-unproved");
  // Reuse fixed DEX widths. Payload-bearing bodies are unsupported here until
  // their complete payload/branch boundary owner is provided; never guess PC.
  bool boundary=false;for(size_t at=0;at<code.units;){const auto word=code.instructions[at];const auto width=dex_instruction::Width(word&255u);
   if(!width||width>code.units-at||(!(word&255u)&&word))return deny("shadow-source-instruction-boundary-unproved");if(at==pc)boundary=true;at+=width;}
  if(!boundary)return deny("shadow-source-current-dex-pc-not-instruction");
  void*again=nullptr,*again_thread=nullptr,*again_shadow=nullptr;uint32_t again_pc=UINT32_MAX;
  if(!chain_.Validate()||!stack_.ShadowHead(visitor,extent,&again_thread,&again_shadow)||again_thread!=thread||again_shadow!=shadow||!stack_.Method(visitor,extent,&again)||again!=method||!stack_.DexPc(visitor,extent,&again_pc)||again_pc!=pc||catalog_.Lookup(method,roots_,read)!=entry)return deny("shadow-source-current-cursor-or-root-changed");
  out->source_=this;out->visitor_=visitor;out->extent_=extent;out->shadow_=shadow;out->method_=method;out->pc_=pc;out->entry_=entry;out->code_=code;return true;
 }
 bool Validate(const Receipt&receipt,const modern_field_metadata::ReadContext&read,const char**failure=nullptr)const noexcept {
  if(receipt.source_!=this){if(failure)*failure="shadow-source-receipt-owner-unproved";return false;}
  Receipt current;if(!Observe(receipt.visitor_,receipt.extent_,read,&current,failure))return false;
  const auto&a=receipt.code_;const auto&b=current.code_;const auto&x=a.source_dex;const auto&y=b.source_dex;
  if(receipt.shadow_!=current.shadow_||receipt.method_!=current.method_||receipt.pc_!=current.pc_||receipt.entry_!=current.entry_||
   a.instructions!=b.instructions||a.units!=b.units||a.registers!=b.registers||a.ins_size!=b.ins_size||a.tries_size!=b.tries_size||a.method_ids!=b.method_ids||a.call_sites!=b.call_sites||a.code_item!=b.code_item||a.compact!=b.compact||
   x.begin!=y.begin||x.size!=y.size||x.data_begin!=y.data_begin||x.data_size!=y.data_size||x.class_def_index!=y.class_def_index){if(failure)*failure="shadow-source-receipt-current-code-or-cursor-changed";return false;}return true;
 }
 private:
 const stack_visitor_service::Service&stack_;const native_managed_stack_chain::Chain&chain_;
 const native_boot_aot_unaffected::Source&window_;const native_boot_method_catalog::Rooted&catalog_;
 const source_handle_roots::StoppedReader&roots_;method_metadata::Layout layout_;
};
}
