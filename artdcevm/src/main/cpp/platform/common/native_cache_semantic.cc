#include "platform/common/native_cache_semantic.h"
#include <cstring>
#include <limits>
namespace artdcevm::native_cache_semantic {
namespace {
bool Fail(std::string* e,const char* s){if(e)*e=s;return false;}
// Exact fixed reflective_value_visitor.h declarations: a polymorphic
// ReflectionSourceInfo(type), then DexCacheSourceInfo(size_t index,ObjPtr cache).
// Independent original callbacks must prove all three values before mutation.
struct SourcePrefix {void* vtable;uint32_t kind;uint32_t padding;size_t index;uintptr_t cache;};
static_assert(offsetof(SourcePrefix,kind)==8&&offsetof(SourcePrefix,index)==16&&offsetof(SourcePrefix,cache)==24);
constexpr uint32_t kMethod=8,kField=9;
class Visitor final:public art::ReflectiveValueVisitor {
 public:
  Visitor(void* cache,uint32_t fields,uint32_t methods,bool clear=false)
      :cache_(cache),fields_(fields),methods_(methods),clear_(clear){}
  art::ArtMethod* VisitMethod(art::ArtMethod* p,const art::ReflectionSourceInfo& s)override{
    Observe(p,s,kMethod,methods_);return clear_&&valid_?nullptr:p;
  }
  art::ArtField* VisitField(art::ArtField* p,const art::ReflectionSourceInfo& s)override{
    Observe(p,s,kField,fields_);return clear_&&valid_?nullptr:p;
  }
  void Expect(uint32_t f,void* field,uint32_t m,void* method){fi_=f;fp_=field;mi_=m;mp_=method;}
  bool Valid()const{return valid_;}
  bool Witness()const{return valid_&&method_hits_&&(!field_count_||field_hits_);}
  bool Empty()const{return valid_&&!field_count_&&!method_count_;}
 private:
  void Observe(void* p,const art::ReflectionSourceInfo& info,uint32_t kind,uint32_t bound){
    SourcePrefix source{};std::memcpy(&source,&info,sizeof(source));
    if(source.kind!=kind||source.cache!=uintptr_t(cache_)||source.index>=bound||!p){valid_=false;return;}
    if(kind==kMethod){++method_count_;if(source.index==mi_){if(p!=mp_)valid_=false;else ++method_hits_;}}
    else {++field_count_;if(source.index==fi_){if(p!=fp_)valid_=false;else ++field_hits_;}}
  }
  void* cache_;uint32_t fields_,methods_;bool clear_,valid_=true;
  uint32_t fi_=UINT32_MAX,mi_=UINT32_MAX;void* fp_=nullptr,*mp_=nullptr;
  size_t field_count_=0,method_count_=0,field_hits_=0,method_hits_=0;
};
}
bool Service::Bind(HookEngine& hooks,std::string* e){
 if(StoragePrepared()||calibrated_)return Fail(e,"Semantic cache owner already bound");
 Leaves value;
 value.visit=reinterpret_cast<Visit>(hooks.FindSymbol("libart.so","_ZN3art6mirror8DexCache22VisitReflectiveTargetsEPNS_22ReflectiveValueVisitorE"));
 value.type=reinterpret_cast<Entry>(hooks.FindSymbol("libart.so","_ZN3art6mirror8DexCache21GetResolvedTypesEntryEj"));
 value.method=reinterpret_cast<Entry>(hooks.FindSymbol("libart.so","_ZN3art6mirror8DexCache23GetResolvedMethodsEntryEj"));
 if(!value.visit||!value.type||!value.method)return Fail(e,"Original semantic cache traversal/type/method entry declarations unavailable");
 leaves_=value;return true;
}
bool Service::Types(void* cache,uint32_t count,type_cache::View* out,const modern_field_metadata::ReadContext* context)const noexcept{
 return StoragePrepared()&&reader_->Read(*this,cache,count,out,context);
}
bool Service::Calibrate(void* cache,uint32_t types,uint32_t fields,uint32_t methods,
 uint32_t ti,void* klass,uint32_t fi,void* field,uint32_t mi,void* method,bool resolved,
 const modern_field_metadata::ReadContext* context,std::string* e){
 type_cache::View view;
 if(!resolved||!field||!method||!klass||ti>=types||fi>=fields||mi>=methods||
    !Types(cache,types,&view,context)||leaves_.type(cache,ti)!=klass||leaves_.method(cache,mi)!=method)
  return Fail(e,"Semantic cache original resolution/entry oracle rejected");
 const auto expected=uintptr_t(klass);bool occupied=false;
 if(expected>UINT32_MAX)return Fail(e,"Semantic type entry is not a release compressed root");
 if(view.full){if(__atomic_load_n(view.full+ti,__ATOMIC_RELAXED)!=expected)return Fail(e,"Semantic full type differs from original entry");occupied=true;}
 else if(view.pairs){type_cache::Pair p;__atomic_load(view.pairs+ti%1024,&p,__ATOMIC_ACQUIRE);if(p.object!=expected||p.index!=ti)return Fail(e,"Semantic type pair differs from original entry");occupied=true;}
 Visitor read(cache,fields,methods);read.Expect(fi,field,mi,method);leaves_.visit(cache,&read);
 if(!occupied||!read.Witness())return Fail(e,"Original cache visitor kind/index/owner/native-member oracle rejected");
 calibrated_=true;return true;
}
bool Service::CheckWitness(void* cache,uint32_t fields,uint32_t methods,uint32_t fi,void* field,uint32_t mi,void* method)const noexcept{
 if(!calibrated_||!cache||!field||!method||fi>=fields||mi>=methods||leaves_.method(cache,mi)!=method)return false;
 Visitor read(cache,fields,methods);read.Expect(fi,field,mi,method);leaves_.visit(cache,&read);return read.Witness();
}
bool Service::Readonly(void* cache,uint32_t fields,uint32_t methods)const noexcept{
 if(!calibrated_||!cache)return false;Visitor read(cache,fields,methods);leaves_.visit(cache,&read);return read.Valid();
}
bool Service::Clear(void* cache,uint32_t fields,uint32_t methods)const noexcept{
 if(!Readonly(cache,fields,methods))return false;
 Visitor write(cache,fields,methods,true);leaves_.visit(cache,&write);
 return write.Valid()&&Cleared(cache,fields,methods);
}
bool Service::Cleared(void* cache,uint32_t fields,uint32_t methods)const noexcept{
 if(!calibrated_||!cache)return false;Visitor read(cache,fields,methods);leaves_.visit(cache,&read);return read.Empty();
}
}
