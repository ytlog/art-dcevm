#include "hooks/shadow_hook_engine.h"
#include "shadowhook.h"
#include "support/declared_symbol_lookup.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
namespace {bool exact=false,open=true,unique=true;int target;unsigned closes=0;std::vector<std::string> requests;}
int shadowhook_init(int,bool){return 0;}
const char*shadowhook_to_errmsg(int){return "stub";}
void*shadowhook_dlopen(const char*){return open?&target:nullptr;}
void*shadowhook_dlsym_dynsym(void*,const char*name){requests.emplace_back(std::string("dyn:")+name);return exact?&target:nullptr;}
extern "C" void*shadowhook_dlsym_declared_symbol_symtab(void*handle,const char*name){assert(handle==&target);requests.emplace_back(std::string("declared:")+name);return unique?&target:nullptr;}
void shadowhook_dlclose(void*){++closes;}
void*shadowhook_hook_sym_addr(void*,void*,void**){return nullptr;}
int shadowhook_get_errno(){return 0;}
int shadowhook_unhook(void*){return 0;}
int main(){artdcevm::ShadowHookEngine engine;const char*name="_Zfixed";
 exact=true;assert(engine.FindSymbol("libart.so",name)==&target&&requests.size()==1&&closes==1);
 exact=false;requests.clear();assert(engine.FindSymbol("libart.so",name)==&target);assert(requests==(std::vector<std::string>{"dyn:_Zfixed","declared:_Zfixed"})&&closes==2);
 unique=false;requests.clear();assert(!engine.FindSymbol("libart.so",name)&&requests.size()==2&&closes==3);
 open=false;requests.clear();assert(!engine.FindSymbol("libart.so",name)&&requests.empty()&&closes==3);
 puts("HookEngine exact dynsym/strict official declaration lookup, no project ELF/XZ parser and balanced handles PASS");
}
