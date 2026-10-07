#include "platform/android13/object_replacement_map_abi.h"
#include "obj_ptr-inl.h"
#include "mirror/object.h"
#include <unordered_map>
#include <unordered_set>
extern "C" bool ArtDcevmSourceProbeObjectReplacementMap(const void* raw,
 const artdcevm::android13::object_map_abi::Node* nodes,size_t count) {
 using Ptr=art::ObjPtr<art::mirror::Object>;
 using Map=std::unordered_map<Ptr,Ptr,art::HashObjPtr>;
 using Fixed=artdcevm::android13::object_map_abi::Map;
 static_assert(!art::kObjPtrPoisoning,"Readonly map uses the fixed release ObjPtr declaration");
 static_assert(sizeof(Ptr)==8 && sizeof(Map)==sizeof(Fixed));
 static_assert(alignof(Map)==alignof(Fixed));
 const auto& map=*static_cast<const Map*>(raw);
 const auto& fixed=*static_cast<const Fixed*>(raw);
 if(map.size()!=count || map.bucket_count()!=fixed.bucket_count || map.max_load_factor()!=1.0f)return false;
 std::unordered_set<void*> visited;
 for(const auto& entry:map){void* key=entry.first.Ptr();bool found=false;
   for(size_t i=0;i<count;++i)if(nodes[i].key==key){if(nodes[i].value!=entry.second.Ptr())return false;found=true;break;}
   if(!found||!visited.insert(key).second)return false;
 }
 if(visited.size()!=count)return false;
 for(size_t i=0;i<count;++i){auto key=Ptr(static_cast<art::mirror::Object*>(nodes[i].key));
   if(art::HashObjPtr{}(key)!=nodes[i].hash)return false;
   auto it=map.find(key);if(it==map.end() || it->second.Ptr()!=nodes[i].value)return false;
   size_t bucket=map.bucket(key);bool in_bucket=false;
   for(auto local=map.begin(bucket);local!=map.end(bucket);++local)if(local->first==key)in_bucket=true;
   if(!in_bucket)return false;
 }
 auto missing=Ptr(reinterpret_cast<art::mirror::Object*>(0x70000008));
 bool expected_missing=true;for(size_t i=0;i<count;++i)if(nodes[i].key==missing.Ptr())expected_missing=false;
 return !expected_missing || map.find(missing)==map.end();
}
