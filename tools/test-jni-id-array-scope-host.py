#!/usr/bin/env python3
"""Exercise ART-style ID-map repair for methods outside declaring arrays."""
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
from evidence_paths import new_evidence_directory
ROOT=Path(__file__).resolve().parents[1]
SOURCE=r'''
#include "platform/common/jni_id_array_scope.h"
#include "platform/abi/arm64/common/runtime_objects.h"
#include <array>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
using namespace artdcevm;
using namespace abi::arm64;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Owner {uint32_t ext,flags;std::map<ArtMethod*,size_t> cache;};
Owner a{11,0x601,{}},b{22,0x601,{}};
ArtMethod old_owned{},new_owned{},old_copy{},new_copy{},same_copy{};
std::map<ArtMethod*,Owner*> owners{{&old_owned,&a},{&new_owned,&b},{&old_copy,&a},{&new_copy,&b},{&same_copy,&a}};
std::set<ArtMethod*> owned{&old_owned,&new_owned};
uint32_t* Unowned(ArtMethod* method) noexcept {if(!method||owned.count(method))return nullptr;return &owners.at(method)->ext;}
struct Mapping final:ReflectiveValueVisitor {
 std::map<ArtMethod*,ArtMethod*> next;
 ArtMethod* VisitMethod(ArtMethod* method,const ReflectionSourceInfo&) override {
  Require(a.ext==11&&b.ext==22,"prior cache mask escaped into the original mapping");return next.at(method);
 }
 ArtField* VisitField(ArtField* field,const ReflectionSourceInfo&) override {
  Require(a.ext==11&&b.ext==22,"method mask escaped into field repair");return field;
 }
};
// This is the fixed JniIdManager contract: update the ID, then repair old/new
// class arrays only when their ClassExt is present. An unowned slot is invalid.
void Repair(std::vector<ArtMethod*>& ids,ReflectiveValueVisitor& visitor){
 ReflectionSourceInfo source{nullptr,kSourceJniMethodId};
 for(size_t id=0;id<ids.size();++id){
  auto* old=ids[id];auto* next=visitor.VisitMethod(old,source);ids[id]=next;
  if(old==next)continue;
  auto* x=owners.at(old);auto* y=owners.at(next);
  if(x->ext){Require(owned.count(old),"old method is outside its declaring array");x->cache[old]=0;}
  if(y->ext){Require(owned.count(next),"new method is outside its declaring array");y->cache[next]=id+1;}
 }
}
int main(){try{
 int cases=0;
 for(auto pair:std::array<std::pair<ArtMethod*,ArtMethod*>,5>{{
   {&old_owned,&new_owned},{&old_copy,&new_copy},{&old_owned,&new_copy},{&old_copy,&new_owned},{&old_copy,&same_copy}}}){
  Mapping mapping;mapping.next[pair.first]=pair.second;
  a.cache.clear();b.cache.clear();std::vector<ArtMethod*> ids{pair.first};
  {
   member_mapping::JniIdArrayVisitor visitor(&mapping,Unowned);Repair(ids,visitor);
   Require(ids[0]==pair.second,"ART did not retain ownership of the ID-map update");
   ReflectionSourceInfo field{nullptr,kSourceJniFieldId};visitor.VisitField(nullptr,field);
  }
  Require(a.ext==11&&b.ext==22&&a.flags==0x601&&b.flags==0x601,"ClassExt/flags were not preserved");
  if(owned.count(pair.first))Require(a.cache[pair.first]==0,"valid old cache was not cleared");
  if(owned.count(pair.second))Require(b.cache[pair.second]==1,"valid new cache was not repaired");
  cases++;
 }
 // A real manager visits several IDs without ending the wrapper's scope.
 // A later ordinary/unchanged method must observe the complete prior ClassExt.
 {
  Mapping sequence;sequence.next={{&old_copy,&new_copy},{&old_owned,&new_owned},{&same_copy,&same_copy}};
  std::vector<ArtMethod*> ids{&old_copy,&old_owned,&same_copy};
  member_mapping::JniIdArrayVisitor visitor(&sequence,Unowned);Repair(ids,visitor);
  Require(ids==std::vector<ArtMethod*>{&new_copy,&new_owned,&same_copy},"sequential ID mapping was lost");
  Require(a.ext==11&&b.ext==22,"unchanged method retained a previous cache mask");
 }
 Mapping mapping;mapping.next[&old_copy]=&new_copy;
 try{member_mapping::JniIdArrayVisitor visitor(&mapping,Unowned);ReflectionSourceInfo source{nullptr,kSourceJniMethodId};
     visitor.VisitMethod(&old_copy,source);throw std::runtime_error("injected native unwind");}
 catch(const std::runtime_error&){}
 Require(a.ext==11&&b.ext==22,"early return/unwind leaked ClassExt masks");
 std::cout<<"JNI ID array ownership PASS cases="<<cases<<" plus sequential IDs and unwind\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
'''
def main():
 evidence=new_evidence_directory(ROOT/'build/evidence/host/jni-id-array-scope')
 helper=runpy.run_path(str(ROOT/'tools/test-structural-policy-host.py'));include=helper['java_home'](None)/'include'
 platform='darwin' if (include/'darwin').exists() else 'linux'
 with tempfile.TemporaryDirectory(dir=evidence) as temp:
  work=Path(temp);source=work/'test.cc';source.write_text(SOURCE);binary=work/'test'
  r=subprocess.run([shutil.which('clang++') or shutil.which('c++'),'-std=c++17','-Wall','-Wextra','-Werror',
      '-I'+str(ROOT/'artdcevm/src/main/cpp'),'-I'+str(include),'-I'+str(include/platform),source,'-o',binary],capture_output=True,text=True)
  (evidence/'compile.log').write_text(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError('JNI ID ownership compilation failed; inspect ignored evidence')
  r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30);(evidence/'test.log').write_text(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError('JNI ID ownership repair failed; inspect ignored evidence')
  print(r.stdout.strip())
 print('Evidence: '+str(evidence.relative_to(ROOT)))
if __name__=='__main__':main()
