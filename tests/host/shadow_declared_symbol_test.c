// Compile the unchanged official xDL address resolver against boundary doubles.
// Strictly selected names use the same official loaded symbol cache; this test
// supplies independent native identities in synthetic ELF symbol records.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
typedef struct {uint32_t st_name;uint8_t st_info,st_other;uint16_t st_shndx;uint64_t st_value,st_size;} Elf64_Sym;
#define ElfW(x) Elf64_##x
#define STT_FUNC 2
#define STT_OBJECT 1
#define STT_TLS 6
#define STT_GNU_IFUNC 10
#define ELF_ST_TYPE(n) ELF64_ST_TYPE(n)
#define __ANDROID_API_R__ 30
#define AT_HWCAP 16
#define AT_HWCAP2 26
#define ELF64_ST_TYPE(n) ((n)&15)
#define XDL_SYMTAB_IS_EXPORT_SYM(n) ((n)!=0 && (n)<0xff00)
typedef struct {uintptr_t load_bias;ElfW(Sym)*symtab;size_t symtab_cnt;char*strtab;size_t strtab_sz;} xdl_t;
static unsigned loads,resolves;
static ElfW(Sym)* expected;
static int independent_addresses[12];
static void xdl_ensure_symtab_loaded(xdl_t*self){assert(self);++loads;}
static int xdl_util_get_api_level(void){return 30;}
#if defined(__aarch64__) || defined(__arm__)
static unsigned long (*getauxval)(unsigned long);
#endif
static ElfW(Sym)*xdl_sym_by_addr(xdl_t*self,void*p){(void)self;(void)p;return NULL;}
static ElfW(Sym)*xdl_dsym_by_addr(xdl_t*self,void*p){(void)self;(void)p;return NULL;}
#define xdl_resolve_symbol_address official_resolve_symbol_address
#include "official_resolver.inc"
#undef xdl_resolve_symbol_address
static void*xdl_resolve_symbol_address(xdl_t*self,ElfW(Sym)*sym,size_t*size){assert(self&&sym==expected);assert(size==NULL);++resolves;return official_resolve_symbol_address(self,sym,size);}
#include "support/shadowhook_metadata/unique_declared_symbol.inc"
static ElfW(Sym) symbols[12];static char strings[2048];static xdl_t module;
static void setup(const char*const*names,size_t count){assert(count<=12);memset(symbols,0,sizeof(symbols));memset(strings,0,sizeof(strings));size_t at=1;for(size_t i=0;i<count;++i){size_t len=strlen(names[i])+1;assert(at+len<sizeof(strings));memcpy(strings+at,names[i],len);symbols[i]=(ElfW(Sym)){.st_name=(uint32_t)at,.st_info=STT_FUNC,.st_shndx=1,.st_value=(uintptr_t)&independent_addresses[i]};at+=len;}module=(xdl_t){0,symbols,count,strings,at};loads=resolves=0;expected=NULL;}
static void reject(const char*name){assert(shadowhook_dlsym_declared_symbol_symtab(&module,name)==NULL&&resolves==0);}
static void accept(const char*name,size_t index){expected=&symbols[index];assert(shadowhook_dlsym_declared_symbol_symtab(&module,name)==&independent_addresses[index]&&resolves==1&&loads==1);}
int main(void){
 const char*single[]={"fixed.__uniq.123"};setup(single,1);accept("fixed",0);
 const char*lto[]={"unrelated","fixed.__uniq.123.llvm.456"};setup(lto,2);accept("fixed",1);
 const char*ambiguous[]={"fixed.__uniq.1","fixed.__uniq.2"};setup(ambiguous,2);reject("fixed");
 const char*exact[]={"other","fixed","fixed.__uniq.1","fixed.__uniq.2"};setup(exact,4);symbols[1].st_info=STT_OBJECT;accept("fixed",1);
 const char*duplicates[]={"fixed","fixed"};setup(duplicates,2);reject("fixed");
 const char*duplicate_clones[]={"fixed.__uniq.1","fixed.__uniq.1"};setup(duplicate_clones,2);reject("fixed");
 const char*bad[]={"fixed.__uniq.","fixed.__uniq..llvm.1","fixed.__uniq.1.llvm.","fixed.__uniq.1.llvm.a","fixed.__uniq.1.llvm.2.llvm.3","fixed.__uniq.1.__uniq.2","fixed.__uniq.-1","fixed.__uniq.1$evil","fixed.llvm.1","fixed.__uniq.1.llvm2"};
 for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i){setup(&bad[i],1);reject("fixed");}
 const char*foreign[]={"fixedElse.__uniq.1"};setup(foreign,1);reject("fixed");
 setup(single,1);symbols[0].st_info=STT_OBJECT;reject("fixed");
 setup(single,1);symbols[0].st_shndx=0;reject("fixed");
 const char*steal[]={"fixed.__uniq.1$evil","fixed.__uniq.1"};setup(steal,2);accept("fixed",1);
 const char*exact_steal[]={"fixed.unknown","fixed"};setup(exact_steal,2);accept("fixed",1);
 setup(single,1);module.strtab_sz--;reject("fixed");
 setup(single,1);symbols[0].st_name=2048;reject("fixed");
 setup(single,1);assert(!shadowhook_dlsym_declared_symbol_symtab(NULL,"fixed"));assert(!shadowhook_dlsym_declared_symbol_symtab(&module,NULL));reject("");
 const char*late_exact[]={"fixed.__uniq.1","fixed.__uniq.2","fixed"};setup(late_exact,3);accept("fixed",2);
 puts("Official xDL declaration lookup: exact data/functions, unique numeric clones, ambiguous/malicious/foreign names refused; exact-first independent of ELF order; same cache and unchanged actual official resolver, no extra decoder PASS");
}
