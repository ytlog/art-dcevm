// Synthetic metadata regression for the actual official-xDL extension.
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// Standard ELF64 wire declarations, kept local because macOS has no elf.h.
typedef struct {uint32_t st_name; uint8_t st_info,st_other; uint16_t st_shndx; uint64_t st_value,st_size;} Elf64_Sym;
typedef struct {uint32_t p_type,p_flags;uint64_t p_offset,p_vaddr,p_paddr,p_filesz,p_memsz,p_align;} Elf64_Phdr;
#define STT_OBJECT 1
#define STT_FUNC 2
#define STB_GLOBAL 1
#define ELF64_ST_INFO(b,t) (((b)<<4)+(t))
#define ELF64_ST_TYPE(i) ((i)&15)
#define SHN_UNDEF 0
#define SHN_LORESERVE 0xff00
#define SHN_HIRESERVE 0xffff
#define SHN_ABS 0xfff1
#define PT_LOAD 1
#define PF_X 1
#define PF_R 4
#ifndef ElfW
#define ElfW(x) Elf64_##x
#endif
#define XDL_SYMTAB_IS_EXPORT_SYM(n) ((n) != SHN_UNDEF && !((n) >= SHN_LORESERVE && (n) <= SHN_HIRESERVE))
typedef struct {
  uintptr_t load_bias;
  const ElfW(Phdr)* dlpi_phdr;
  size_t dlpi_phnum;
  ElfW(Sym)* dynsym;
  const char* dynstr;
  size_t dynstr_sz;
  struct {size_t buckets_cnt;} gnu_hash, sysv_hash;
  ElfW(Sym)* symtab;
  size_t symtab_cnt;
  char* strtab;
  size_t strtab_sz;
} xdl_t;
static void xdl_ensure_dynsym_loaded(xdl_t* p) {(void)p;}
static void xdl_ensure_symtab_loaded(xdl_t* p) {(void)p;}
static ElfW(Sym)* xdl_dynsym_find_symbol_use_gnu_hash(xdl_t* p,const char* n) {(void)n;return p->dynsym;}
static ElfW(Sym)* xdl_dynsym_find_symbol_use_sysv_hash(xdl_t* p,const char* n) {(void)n;return p->dynsym;}
#include "support/shadowhook_metadata/exact_function_extent.inc"
int main(void) {
  char names[] = "\0exact\0exact.llvm.42\0";
  ElfW(Sym) symbols[2] = {{.st_name=1,.st_info=ELF64_ST_INFO(STB_GLOBAL,STT_FUNC),.st_shndx=1,.st_value=0x120,.st_size=0x20}};
  ElfW(Phdr) ph = {.p_type=PT_LOAD,.p_flags=PF_R|PF_X,.p_vaddr=0x100,.p_memsz=0x100};
  xdl_t module = {.load_bias=0x1000,.dlpi_phdr=&ph,.dlpi_phnum=1,.symtab=symbols,.symtab_cnt=1,.strtab=names,.strtab_sz=sizeof(names)};
  uintptr_t begin=0; size_t size=0;
#define RESULT() shadowhook_get_named_function_extent(&module,"exact",(void*)0x1120,&begin,&size)
  assert(RESULT()==0 && begin==0x1120 && size==0x20);
  symbols[0].st_name=7; assert(RESULT()!=0 && !begin && !size); symbols[0].st_name=1;
  symbols[0].st_info=STT_OBJECT; assert(RESULT()!=0); symbols[0].st_info=STT_FUNC;
  symbols[0].st_size=0; assert(RESULT()!=0); symbols[0].st_size=0x20;
  symbols[0].st_shndx=SHN_ABS; assert(RESULT()!=0); symbols[0].st_shndx=1;
  ph.p_flags=PF_R; assert(RESULT()!=0); ph.p_flags|=PF_X;
  ph.p_memsz=0x30; assert(RESULT()!=0); ph.p_memsz=0x100;
  symbols[1]=symbols[0];symbols[1].st_size=0x30;module.symtab_cnt=2;assert(RESULT()!=0);module.symtab_cnt=1;
  assert(shadowhook_get_named_function_extent(&module,"exact",(void*)0x1124,&begin,&size)!=0);
  module.load_bias=UINTPTR_MAX-0x10;assert(RESULT()!=0);module.load_bias=0x1000;
  symbols[0].st_size=UINT64_MAX;assert(RESULT()!=0);symbols[0].st_size=0x20;
  module.strtab_sz=6;assert(RESULT()!=0);module.strtab_sz=sizeof(names);
  module.dynsym=symbols;module.dynstr=names;module.dynstr_sz=sizeof(names);module.gnu_hash.buckets_cnt=1;
  assert(RESULT()==0);module.symtab=NULL;assert(RESULT()==0);
  puts("named-symbol-extent exact/type/size/address/PF_X guards PASS");
}
