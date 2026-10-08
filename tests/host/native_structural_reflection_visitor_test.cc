#include "platform/common/native_structural_reflection_visitor.h"
#include <cassert>
#include <cstdio>
#include <array>
namespace art {
class ReflectionSourceInfo {public:virtual ~ReflectionSourceInfo()=default;uint32_t type=10;uint32_t pad=0;void* object=nullptr;};
}
int main(){
 using artdcevm::native_structural_reflection::detail::Visitor;
 alignas(8) std::array<unsigned char,32> old{},kept{},gone{},unknown{};
 uint32_t flags=0x109;std::memcpy(kept.data()+4,&flags,4);
 std::unordered_map<void*,void*> methods{{old.data(),kept.data()},{gone.data(),nullptr}};
 std::unordered_map<void*,void*> fields{{old.data(),kept.data()},{gone.data(),nullptr}};
 Visitor preflight(methods,fields,true),update(methods,fields,false);
 art::ReflectiveValueVisitor* native=&update;
 art::ReflectionSourceInfo info;
 auto method=[](void* p){return reinterpret_cast<art::ArtMethod*>(p);};
 auto field=[](void* p){return reinterpret_cast<art::ArtField*>(p);};
 assert(native->VisitMethod(method(old.data()),info)==method(kept.data()));
 assert(native->VisitMethod(method(gone.data()),info)==method(gone.data()));
 assert(native->VisitMethod(method(unknown.data()),info)==method(unknown.data()));
 assert(native->VisitMethod(nullptr,info)==nullptr);
 info.type=8;assert(native->VisitMethod(method(gone.data()),info)==nullptr);
 info.type=9;assert(native->VisitField(field(gone.data()),info)==nullptr);
 info.type=10;assert(native->VisitField(field(old.data()),info)==field(kept.data()));
 assert(native->VisitField(field(gone.data()),info)==field(gone.data()));
 alignas(8) std::array<unsigned char,40> mirror{};info.type=1;info.object=mirror.data();
 assert(preflight.VisitMethod(method(old.data()),info)==method(old.data()));
 uint32_t value;std::memcpy(&value,mirror.data()+32,4);assert(value==0);
 native->VisitMethod(method(old.data()),info);std::memcpy(&value,mirror.data()+32,4);assert(value==flags);
 info.type=3;assert(preflight.VisitMethod(method(gone.data()),info)==method(gone.data()));assert(preflight.Unsupported());
 Visitor field_preflight(methods,fields,true);info.type=4;
 assert(field_preflight.VisitField(field(gone.data()),info)==field(gone.data()));assert(field_preflight.Unsupported());
 Visitor guarded(methods,fields,true,true,true,true);
 info.type=3;assert(guarded.VisitMethod(method(gone.data()),info)==method(gone.data()));
 info.type=4;assert(guarded.VisitField(field(gone.data()),info)==field(gone.data()));
 info.type=3;assert(guarded.VisitField(field(gone.data()),info)==field(gone.data()));
 assert(!guarded.Unsupported());
 Visitor fields_without_mh(methods,fields,true,true,true);info.type=3;
 assert(fields_without_mh.VisitField(field(gone.data()),info)==field(gone.data()));assert(fields_without_mh.Unsupported());
 // A method-owner proof must never substitute for the separate field owner.
 Visitor only_field_mh(methods,fields,true,false,false,true);
 assert(only_field_mh.VisitMethod(method(gone.data()),info)==method(gone.data()));assert(only_field_mh.Unsupported());
 Visitor only_methods(methods,fields,true,true,false);info.type=4;
 assert(only_methods.VisitField(field(gone.data()),info)==field(gone.data()));assert(only_methods.Unsupported());
 std::puts("Native reflective source visitor: retained mapping, retired IDs, DEX clearing, modifier update, handle refusal passed");
}
