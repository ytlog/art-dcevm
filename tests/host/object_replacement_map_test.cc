#include "platform/android13/object_replacement_map.h"
#include <unordered_map>
#include <unordered_set>
#include <cassert>
#include <cstdio>
#include <thread>
using namespace artdcevm::android13;
// The real host libc++ oracle must accept the fixed Android libc++ map view.
// A changed host ABI/hash fails these checks rather than substituting a mirrored oracle.
#ifndef _LIBCPP_VERSION
#error This oracle requires libc++.
#endif
using PlatformMap=std::unordered_map<void*,void*>;
bool CheckPlatformMap(const void* raw,const object_map_abi::Node* nodes,size_t count){
 static_assert(sizeof(PlatformMap)==sizeof(object_map_abi::Map));
 static_assert(alignof(PlatformMap)==alignof(object_map_abi::Map));
 const auto& map=*static_cast<const PlatformMap*>(raw);assert(map.size()==count);
 std::unordered_set<void*> seen;for(const auto& entry:map){assert(seen.insert(entry.first).second);bool found=false;
 for(size_t i=0;i<count;++i)if(nodes[i].key==entry.first){assert(nodes[i].value==entry.second);found=true;break;}assert(found);}
 assert(seen.size()==count);
 for(size_t i=0;i<count;++i){assert(std::hash<void*>{}(nodes[i].key)==nodes[i].hash);auto found=map.find(nodes[i].key);assert(found!=map.end()&&found->second==nodes[i].value);
 bool local=false;for(auto it=map.begin(map.bucket(nodes[i].key));it!=map.end(map.bucket(nodes[i].key));++it)if(it->first==nodes[i].key)local=true;assert(local);}
 assert(map.find(reinterpret_cast<void*>(0x70000008))==map.end());return true;
}
int main(){std::string error;
 std::vector<std::pair<void*,void*>> entries;PlatformMap oracle;
 for(uintptr_t i=1;i<=1000;++i){void* key=reinterpret_cast<void*>(0xf000000000000000ULL+i*0x10000008ULL),*value=reinterpret_cast<void*>(i*8);entries.emplace_back(key,value);oracle.emplace(key,value);}
 entries.emplace_back(entries[0].first,reinterpret_cast<void*>(0xdeadbeef));
 for(size_t buckets:{size_t{2},size_t{4},size_t{128},size_t{2048}}){ObjectReplacementMap map(entries,buckets);assert(map.Size()==1000);assert(VerifyObjectReplacementMap(map,&error));assert(CheckPlatformMap(map.BorrowedMap(),map.Nodes(),map.Size()));
 const auto& view=*static_cast<const PlatformMap*>(map.BorrowedMap());for(const auto& item:oracle)assert(view.at(item.first)==item.second);
 std::thread reader([&]{for(unsigned round=0;round<5;++round)for(const auto& item:oracle)assert(view.at(item.first)==item.second);});
 for(unsigned round=0;round<5;++round)for(const auto& item:oracle)assert(view.at(item.first)==item.second);reader.join();}
 ObjectReplacementMap empty({});assert(VerifyObjectReplacementMap(empty,&error));assert(CheckPlatformMap(empty.BorrowedMap(),empty.Nodes(),empty.Size()));
 ObjectReplacementMap one({{reinterpret_cast<void*>(8),reinterpret_cast<void*>(16)}});assert(one.Size()==1&&VerifyObjectReplacementMap(one,&error));
 for(uintptr_t value:{uintptr_t{0},uintptr_t{1},uintptr_t{0xffffffff},uintptr_t{0x100000000},uintptr_t{0xffffffffffffffffULL}})assert(HashReplacementPointer(reinterpret_cast<void*>(value))==std::hash<void*>{}(reinterpret_cast<void*>(value)));
 // Malformed borrowed views must fail before calling the real ART consumer.
 ObjectReplacementMap damaged(entries,128);
 auto& header=*const_cast<object_map_abi::Map*>(static_cast<const object_map_abi::Map*>(damaged.BorrowedMap()));
 auto saved_header=header;
 header.size++;assert(!VerifyObjectReplacementMap(damaged,&error));header=saved_header;
 header.buckets=reinterpret_cast<object_map_abi::NodeBase**>(1);assert(!VerifyObjectReplacementMap(damaged,&error));header=saved_header;
 header.max_load_factor=2;assert(!VerifyObjectReplacementMap(damaged,&error));header=saved_header;
 header.sentinel.next=reinterpret_cast<object_map_abi::NodeBase*>(1);assert(!VerifyObjectReplacementMap(damaged,&error));header=saved_header;
 auto* nodes=const_cast<object_map_abi::Node*>(damaged.Nodes());auto saved=nodes[0];
 nodes[0].hash^=1;assert(!VerifyObjectReplacementMap(damaged,&error));nodes[0]=saved;
 nodes[0].next=&nodes[0];assert(!VerifyObjectReplacementMap(damaged,&error));nodes[0]=saved;
 nodes[1].key=nodes[0].key;nodes[1].hash=nodes[0].hash;assert(!VerifyObjectReplacementMap(damaged,&error));
 ObjectReplacementMap bucket_damage(entries,128);
 auto& buckets=*static_cast<const object_map_abi::Map*>(bucket_damage.BorrowedMap());
 auto* before=buckets.buckets[0];buckets.buckets[0]=reinterpret_cast<object_map_abi::NodeBase*>(1);
 assert(!VerifyObjectReplacementMap(bucket_damage,&error));buckets.buckets[0]=before;
 assert(VerifyObjectReplacementMap(bucket_damage,&error));
 bool rejected=false;try{ObjectReplacementMap bad(entries,3);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 puts("host libc++ readonly replacement map: fixed pointer hash, actual const find/iteration/local-buckets, duplicate first wins, high bits, collisions, concurrent reads passed");}
