#include "platform/common/readable_mapping_scope.h"
#include "platform/common/method_identity_calibration.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace artdcevm::method_identity;
int main(){
 std::vector<unsigned char> classes(60000*256);size_t extent=0;
 const auto before=MappingOsQueries();
 {
  ScopedReadableMappings scope;assert(scope.Ready());
  const auto captured=MappingOsQueries();assert(captured>before);
  for(size_t count:{size_t(30000),size_t(60000)})for(size_t i=0;i<count;++i){
   const auto*address=classes.data()+i*256;
   for(int read=0;read<21;++read)assert(scope.Extent(address,&extent)&&extent>=256);
  }
  assert(MappingOsQueries()==captured);
  extent=99;assert(!scope.Extent(reinterpret_cast<void*>(1),&extent)&&!extent);
  assert(!scope.Extent(nullptr,&extent)&&!scope.Extent(classes.data(),nullptr));
  assert(MappingOsQueries()==captured);
  {
   ScopedReadableMappings nested;assert(nested.Ready());auto count=MappingOsQueries();
   assert(!scope.Extent(classes.data(),&extent));
   assert(nested.Extent(classes.data(),&extent));assert(MappingOsQueries()==count);
  }
  assert(scope.Extent(classes.data(),&extent));
 }
 // A fresh traversal captures after a possible safepoint; it does not retain
 // the previous snapshot or reuse a negative lookup from an earlier window.
 const auto next=MappingOsQueries();{ScopedReadableMappings scope;assert(scope.Ready()&&scope.Extent(classes.data(),&extent));assert(MappingOsQueries()>next);}
 std::puts("Actual ordinary mapping snapshot: full30k/60k x21 reads, zero per-node OS queries, denied pointer no fallback, nested inactive view refusal and fresh traversal recapture PASS");
}
