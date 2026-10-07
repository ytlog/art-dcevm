#include "evolution/checked_method_resolution.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
namespace g=artdcevm::evolution;
namespace {
struct Provider final:g::CheckedMethodResolution {
  g::MethodResolutionContext expected;
  int result=7,incompatible=8;
  bool cached=false,missing=false,class_failure=false,class_mismatch=false,access_failure=false,
       method_mismatch=false,other_kind=false,cache_during_resolution=false;
  std::string exception;
  std::vector<std::string> calls;
  explicit Provider(g::MethodResolutionContext context):expected(context){}
  void Record(const g::MethodResolutionContext& c,const char* call) {
    assert(c.linker==expected.linker && c.method_index==expected.method_index &&
        c.dex_cache_handle==expected.dex_cache_handle && c.class_loader_handle==expected.class_loader_handle &&
        c.referrer==expected.referrer && c.invoke_type==expected.invoke_type);
    calls.emplace_back(call);
  }
  void* CachedMethod(const g::MethodResolutionContext& c)override{Record(c,"cached");return cached?&result:nullptr;}
  bool ResolveAndRootClass(const g::MethodResolutionContext& c)override {
    Record(c,"class");if(class_failure)exception="NoClassDefFoundError";
    if(!class_failure && cache_during_resolution)cached=true;
    return !class_failure;
  }
  bool RejectClassInvokeType(const g::MethodResolutionContext& c)override {
    Record(c,"class-type");if(class_mismatch)exception="IncompatibleClassChangeError";return class_mismatch;
  }
  void* FindResolvedMethod(const g::MethodResolutionContext& c)override{Record(c,"find");return missing?nullptr:&result;}
  bool RejectAccess(const g::MethodResolutionContext& c,void* m)override {
    Record(c,"access");assert(m==&result);if(access_failure)exception="IllegalAccessError";return access_failure;
  }
  bool RejectMethodInvokeType(const g::MethodResolutionContext& c,void* m)override {
    Record(c,"method-type");assert(m==&result);return method_mismatch;
  }
  void* FindIncompatibleMethod(const g::MethodResolutionContext& c)override {
    Record(c,"other-kind");return other_kind?&incompatible:nullptr;
  }
  void ThrowInvokeMismatch(const g::MethodResolutionContext& c,void* m)override {
    Record(c,"throw-type");assert(m==&result || m==&incompatible);exception="IncompatibleClassChangeError";
  }
  void ThrowMissingMethod(const g::MethodResolutionContext& c)override{Record(c,"throw-missing");exception="NoSuchMethodError";}
};
void Calls(const Provider& p,std::initializer_list<const char*> expected) {
  std::vector<std::string> names;for(auto name:expected)names.emplace_back(name);assert(p.calls==names);
}
}
int main() {
  int linker,cache,loader,foreign_referrer;
  const g::MethodResolutionContext context{&linker,123,&cache,&loader,&foreign_referrer,4};
  // Cache hit still requires class and member checks. Explicit cache/loader
  // have identities independent of the referrer, even for cached resolution.
  Provider hit(context);hit.cached=true;assert(g::ResolveCheckedMethod(context,hit)==&hit.result);
  Calls(hit,{"cached","class","class-type","access","method-type"});
  Provider fresh(context);assert(g::ResolveCheckedMethod(context,fresh)==&fresh.result);
  Calls(fresh,{"cached","class","class-type","find","access","method-type"});
  auto no_referrer=context;no_referrer.referrer=nullptr;
  Provider nullable(no_referrer);nullable.access_failure=true;
  assert(g::ResolveCheckedMethod(no_referrer,nullable)==&nullable.result && nullable.exception.empty());
  Calls(nullable,{"cached","class","class-type","find","method-type"});
  Provider failed_class(context);failed_class.class_failure=true;
  assert(!g::ResolveCheckedMethod(context,failed_class) && failed_class.exception=="NoClassDefFoundError");
  Calls(failed_class,{"cached","class"});
  // Class/interface mismatch wins over a missing method or access failure.
  Provider wrong_class(context);wrong_class.class_mismatch=true;wrong_class.missing=true;wrong_class.access_failure=true;
  assert(!g::ResolveCheckedMethod(context,wrong_class) && wrong_class.exception=="IncompatibleClassChangeError");
  Calls(wrong_class,{"cached","class","class-type"});
  // Access failure wins over method invoke mismatch, including cache hits.
  Provider access(context);access.cached=true;access.access_failure=true;access.method_mismatch=true;
  assert(!g::ResolveCheckedMethod(context,access) && access.exception=="IllegalAccessError");
  Calls(access,{"cached","class","class-type","access"});
  Provider wrong_method(context);wrong_method.method_mismatch=true;
  assert(!g::ResolveCheckedMethod(context,wrong_method) && wrong_method.exception=="IncompatibleClassChangeError");
  Calls(wrong_method,{"cached","class","class-type","find","access","method-type","throw-type"});
  Provider alternate(context);alternate.missing=true;alternate.other_kind=true;
  assert(!g::ResolveCheckedMethod(context,alternate) && alternate.exception=="IncompatibleClassChangeError");
  Calls(alternate,{"cached","class","class-type","find","other-kind","throw-type"});
  Provider absent(context);absent.missing=true;
  assert(!g::ResolveCheckedMethod(context,absent) && absent.exception=="NoSuchMethodError");
  Calls(absent,{"cached","class","class-type","find","other-kind","throw-missing"});
  // A class-resolution side effect changes which lookup branch is used by
  // ResolveMethodWithChecks. The old explicit-Handle snapshot stays unchanged.
  Provider old_filled(context);old_filled.cache_during_resolution=true;
  assert(g::ResolveCheckedMethod(context,old_filled)==&old_filled.result);
  Calls(old_filled,{"cached","class","class-type","find","access","method-type"});
  Provider new_filled(context);new_filled.cache_during_resolution=true;
  assert(g::ResolveCheckedReferrerMethod(context,new_filled)==&new_filled.result);
  Calls(new_filled,{"class","class-type","cached","access","method-type"});
  Provider modern_hit(context);modern_hit.cached=true;
  assert(g::ResolveCheckedReferrerMethod(context,modern_hit)==&modern_hit.result);
  Calls(modern_hit,{"class","class-type","cached","access","method-type"});
  Provider modern_fresh(context);
  assert(g::ResolveCheckedReferrerMethod(context,modern_fresh)==&modern_fresh.result);
  Calls(modern_fresh,{"class","class-type","cached","find","access","method-type"});
  Provider modern_class_failure(context);modern_class_failure.cached=true;modern_class_failure.class_failure=true;
  assert(!g::ResolveCheckedReferrerMethod(context,modern_class_failure) && modern_class_failure.exception=="NoClassDefFoundError");
  Calls(modern_class_failure,{"class"});
  Provider modern_wrong_class(context);modern_wrong_class.cached=true;modern_wrong_class.class_mismatch=true;
  modern_wrong_class.access_failure=true;modern_wrong_class.method_mismatch=true;
  assert(!g::ResolveCheckedReferrerMethod(context,modern_wrong_class) && modern_wrong_class.exception=="IncompatibleClassChangeError");
  Calls(modern_wrong_class,{"class","class-type"});
  Provider modern_access(context);modern_access.cached=true;modern_access.access_failure=true;modern_access.method_mismatch=true;
  assert(!g::ResolveCheckedReferrerMethod(context,modern_access) && modern_access.exception=="IllegalAccessError");
  Calls(modern_access,{"class","class-type","cached","access"});
  Provider modern_wrong_method(context);modern_wrong_method.method_mismatch=true;
  assert(!g::ResolveCheckedReferrerMethod(context,modern_wrong_method) && modern_wrong_method.exception=="IncompatibleClassChangeError");
  Calls(modern_wrong_method,{"class","class-type","cached","find","access","method-type","throw-type"});
  Provider modern_alternate(context);modern_alternate.missing=true;modern_alternate.other_kind=true;
  assert(!g::ResolveCheckedReferrerMethod(context,modern_alternate) && modern_alternate.exception=="IncompatibleClassChangeError");
  Calls(modern_alternate,{"class","class-type","cached","find","other-kind","throw-type"});
  Provider modern_absent(context);modern_absent.missing=true;
  assert(!g::ResolveCheckedReferrerMethod(context,modern_absent) && modern_absent.exception=="NoSuchMethodError");
  Calls(modern_absent,{"class","class-type","cached","find","other-kind","throw-missing"});
  std::cout<<"checked method resolution: explicit context, nullable referrer, cached checks, class-first referrer checks, cache publication during class resolution and exception precedence passed\n";
}
