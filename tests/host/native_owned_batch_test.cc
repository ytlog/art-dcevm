// Reuse the composed provider's independent JNI/ELF/low-heap boundary fixture.
// Both the source entry callbacks and final commit sequence below are actual TUs.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main composed_provider_boundary
#include "native_source_commit_test.cc"
#undef main
#pragma clang diagnostic pop
#include "platform/common/native_owned_removal.cc"
#include <fstream>
#include <map>
#include <iterator>
using namespace artdcevm::native_owned_removal;
namespace batch_boundary {
std::map<jclass,evolution::ClassSchema> live;
std::map<jclass,jobject> loaders;
std::vector<owned_structural_transaction::StructuralTransaction> partitions;
transaction_root::Holder holder{};std::array<abi::arm64::android11::ClassRedefinition,2> owners{};
struct Prefix {void* vptr=nullptr;const unsigned char* begin=nullptr;};std::array<Prefix,2> dexfiles{};
uint64_t initial_bits=3;bool proof_ok=true,prepare_ok=true,skip_second=false,repeat_second=false;
bool (*before_commit)()=nullptr;
jvmtiError (*before_commit_result)()=nullptr;
void (*enter_final)()=nullptr;void (*leave_final)()=nullptr;
int entries=0,preflights=0,prepared=0,begins=0,replacements=0,original_checks=0,original_commits=0,phase_jni=0;
size_t expected_classes=2;bool descendant_mode=false;int publication_pipe=-1;std::vector<std::string> operations;
std::map<void*,std::vector<void*>> arrays;std::map<void*,void*> moved;
void Reset(){entries=preflights=prepared=begins=replacements=original_checks=original_commits=phase_jni=0;proof_ok=prepare_ok=true;skip_second=repeat_second=false;operations.clear();expected_classes=2;descendant_mode=false;}
bool OriginalCheck(void*){++original_checks;return true;}
void OriginalCommit(void*,const void*){++original_commits;}
void Record(void*){}
bool Ready(void*){return true;}
bool Preflight(JNIEnv*,jvmtiEnv*,const std::vector<Definition>& values,void*,std::string*){assert(!stopped&&values.size()==partitions.size());++preflights;return proof_ok;}
struct CommitServices:source_commit::Services {
 const source_commit::Plan* plan;
 explicit CommitServices(const source_commit::Plan& p):plan(&p){}
 bool Validate(const source_commit::Plan& p)const override{assert(&p==plan&&stopped);operations.push_back("validate");if(descendant_mode){assert(p.old_classes.size()==2&&p.new_classes.size()==2);assert(Read32(p.old_classes[1],offsetof(class_metadata::Class17,super_class))==uintptr_t(p.old_classes[0]));assert(Read32(p.new_classes[1],offsetof(class_metadata::Class17,super_class))==uintptr_t(p.new_classes[0]));}
  return prepare_ok&&p.old_classes.size()==expected_classes&&p.class_root_origins.size()==expected_classes;}
 void BeginCommit(const source_commit::Plan&)const override{++begins;operations.push_back("begin");if(publication_pipe>=0){char mark=1;assert(write(publication_pipe,&mark,1)==1);}}
 uint32_t Status(void*)const override{return 10;}
 void SetStatus(void*,uint32_t)const override{assert(false);}
 void CopyFields(bool is_static,void* target,void*,void* source,void*)const override{assert(stopped&&begins==1);operations.push_back(is_static?"static":"instance");if(!is_static){Put32(target,8,Read32(source,8));Put32(source,8,0);}}
 void* ObjectClass(void* object)const override{return Root(object);}
 void MarkClassAndMethodsObsolete(void*,void*)const override{operations.push_back("obsolete");}
 void UpdateReflectiveReferences(const source_commit::Plan&)const override{operations.push_back("reflection");}
 void ForceDeoptimizeFrames()const override{operations.push_back("frames");}
 void ReplaceObjectReferences()const override{++replacements;operations.push_back("replace");for(auto& entry:arrays)for(auto& value:entry.second)if(moved.count(value))value=moved[value];}
 void RestoreDeclaringClasses(const source_commit::Plan&)const override{operations.push_back("restore");}
 void* NewClassAfterReplacement(void* root,size_t index)const override{assert(replacements==1&&arrays.count(root)&&index<arrays[root].size());return arrays[root][index];}
 void RetainOldClass(void* replacement,void* old)const override{assert(stopped);bool found=false;for(size_t i=0;i<plan->old_classes.size();i++)if(plan->old_classes[i]==old)found=replacement==moved[plan->new_classes[i]];assert(found);operations.push_back("retain");}
 void InvalidateCompiledCode()const override{operations.push_back("jit");}
 void DeoptimizeEverythingForAbstractTransition()const override{assert(false);}
 void ClearInterpreterCaches()const override{operations.push_back("cache");}
};
std::unique_ptr<source_commit::Services> Prepare(const source_commit::Plan& p,void*,std::string*){++prepared;return std::make_unique<CommitServices>(p);}
jvmtiError JNICALL Loader(jvmtiEnv*,jclass klass,jobject* out){assert(!stopped);*out=loaders.at(klass);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Free(jvmtiEnv*,unsigned char* p){assert(!stopped);std::free(p);return JVMTI_ERROR_NONE;}
jvmtiError Entry(jvmtiEnv*,jint count,const jvmtiClassDefinition* definitions){
 assert(!stopped&&size_t(count)==partitions.size());++entries;
 for(int n=0;n<count;n++){bool found=false;for(int i=0;i<count;i++)if(definitions[i].klass==owners[n].klass){auto* parsed=const_cast<Prefix*>(static_cast<const Prefix*>(owners[n].dex_file));found=std::memcmp(definitions[i].class_bytes,parsed->begin,size_t(definitions[i].class_byte_count))==0;parsed->begin=definitions[i].class_bytes;}assert(found);assert(Fields(&owners[n])&&Methods(&owners[n]));}
 if(before_commit&&!before_commit())return before_commit_result?before_commit_result():JVMTI_ERROR_NOT_AVAILABLE;
 phase_jni=jni_calls;if(enter_final)enter_final();stopped=1;
 transaction_root::Iterator first{0,&holder},second{1,&holder};
 Update(&owners[0],&first);
 if(count>1&&transaction_root::Bit(holder.initial_structural,1)&&!skip_second)Update(&owners[1],&second);
 if(count>1&&repeat_second)Update(&owners[1],&second);
 assert(jni_calls==phase_jni);stopped=0;if(leave_final)leave_final();return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Extensions(jvmtiEnv*,jint* count,jvmtiExtensionFunctionInfo** info){assert(!stopped);*count=1;*info=static_cast<jvmtiExtensionFunctionInfo*>(std::calloc(1,sizeof(**info)));(*info)->id=strdup("com.android.art.class.structurally_redefine_classes");(*info)->func=reinterpret_cast<jvmtiExtensionFunction>(Entry);return JVMTI_ERROR_NONE;}
void Build(const std::vector<Definition>& definitions){
 initial_bits=(uint64_t{1}<<definitions.size())-1;holder.initial_structural={&initial_bits,definitions.size(),1};partitions.clear();arrays.clear();moved.clear();
 for(size_t n=0;n<definitions.size();n++){
  owners[n]={};owners[n].klass=definitions[n].klass;dexfiles[n].begin=definitions[n].dex.data();owners[n].dex_file=&dexfiles[n];
  void* previous=heap.New(sizeof(class_metadata::Class17)),*next=heap.New(sizeof(class_metadata::Class17)),*instance=heap.New(16),*fresh=heap.New(16),*final_class=heap.New(sizeof(class_metadata::Class17)),*array=heap.New(16);
  Put32(instance,0,uint32_t(uintptr_t(previous)));Put32(instance,8,uint32_t(71+n));Put32(fresh,0,uint32_t(uintptr_t(next)));
  moved[next]=final_class;arrays[array]={next};
  owned_structural_transaction::StructuralTransaction part;
  part.metadata.count=definitions.size();part.metadata.stride=sizeof(owners[0]);part.metadata.owners.begin=reinterpret_cast<const unsigned char*>(owners.data());part.metadata.initial_structural=holder.initial_structural;
  part.roots.owner=&owners[n];part.roots.index=n;part.roots.initialized=part.roots.actually_structural=part.roots.initial_structural=true;
  part.plan={previous,array,{previous},{next},{instance},{fresh},false};partitions.push_back(part);
 }
}
}
namespace artdcevm::android13 {
int ReadLiveSchema(jvmtiEnv*,JNIEnv*,jclass klass,evolution::ClassSchema* out,std::string*){assert(!stopped);*out=batch_boundary::live.at(klass);return JVMTI_ERROR_NONE;}
}
namespace artdcevm::owned_structural_transaction {
bool ReadStructuralTransaction(HookEngine&,const modern_field_metadata::FieldMetadata& metadata,const void* raw,StructuralTransaction* out,std::string*){
 assert(stopped&&metadata.Calibrated());transaction_root::Iterator it;std::memcpy(&it,raw,sizeof(it));assert(it.holder==&batch_boundary::holder&&it.index>=0&&size_t(it.index)<batch_boundary::partitions.size());*out=batch_boundary::partitions[it.index];return true;
}
}
#ifndef ARTDCEVM_BATCH_BOUNDARY_NAME
#define ARTDCEVM_BATCH_BOUNDARY_NAME main
#endif
int ARTDCEVM_BATCH_BOUNDARY_NAME(int argc,char** argv){
 assert(argc==4);InitJni();InitializeHeap();Hooks hooks;std::string error;
 std::array<modern_field_metadata::FieldOracle,4> oracles{};for(size_t n=0;n<4;n++)oracles[n]={reinterpret_cast<jobject>(&mirrors[n]),names[n],types[n],n==0?8u:1u};
 modern_field_metadata::FieldMetadata metadata;assert(metadata.Bind(hooks,&error));Put32(thread.data(),0,0);assert(metadata.Calibrate({thread.data(),&env,&runtime},reinterpret_cast<jobject>(old_class),"LFixture;",oracles.data(),4,&error));Put32(thread.data(),0,92u<<24);
 std::ifstream in(argv[1],std::ios::binary);std::vector<unsigned char> dex((std::istreambuf_iterator<char>(in)),{});evolution::ClassSchema incoming;assert(evolution::ReadDexDeclarations(dex,&incoming,&error));
 std::vector<Definition> definitions{{reinterpret_cast<jclass>(heap.New(16)),dex},{reinterpret_cast<jclass>(heap.New(16)),dex}};
 using namespace batch_boundary;
 for(size_t n=0;n<2;n++){live[definitions[n].klass]=incoming;live[definitions[n].klass].methods.push_back({"removed","()I",1,0});loaders[definitions[n].klass]=reinterpret_cast<jobject>(heap.New(16));}
 jvmtiInterface_1 functions{};functions.GetClassLoader=Loader;functions.GetExtensionFunctions=Extensions;functions.Deallocate=Free;jvmtiEnv ti{&functions};
 installed=true;fields_original=reinterpret_cast<void*>(OriginalCheck);methods_original=fields_original;commit_original=reinterpret_cast<void*>(OriginalCommit);record_fields=Record;record_methods=Record;
 Provider provider{Ready,Prepare,&metadata,nullptr,true,Preflight};
 Reset();Build(definitions);assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NONE);assert(entries==1&&preflights==1&&prepared==1&&begins==1&&replacements==1&&original_commits==0&&operations.front()=="validate");for(size_t n=0;n<2;n++){assert(Read32(partitions[n].plan.old_instances[0],8)==0);assert(Read32(partitions[n].plan.new_instances[0],8)==71+n);}
 Reset();Build(definitions);std::swap(owners[0],owners[1]);std::swap(partitions[0].plan,partitions[1].plan);assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NONE&&begins==1&&original_commits==0); // actual native order differs from caller order
 Reset();expected_classes=1;std::vector<Definition> one{definitions[0]};Build(one);Provider legacy{Ready,Prepare,&metadata,nullptr,true};assert(Run(hooks,&env,&ti,one[0].klass,one[0].dex,legacy,&error)==JVMTI_ERROR_NONE&&entries==1&&preflights==0&&begins==1);
 Reset();Build(definitions);live[definitions[0].klass]=incoming;assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NONE&&begins==1); // body-only item + removal
 Reset();Build(definitions);proof_ok=false;assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_NOT_AVAILABLE&&entries==0&&begins==0);
 Reset();auto duplicate=definitions;duplicate[1].klass=duplicate[0].klass;assert(RunBatch(hooks,&env,&ti,duplicate,provider,&error)==JVMTI_ERROR_ILLEGAL_ARGUMENT&&entries==0);
 Reset();auto loader=loaders[definitions[1].klass];loaders[definitions[1].klass]=loaders[definitions[0].klass];assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_ILLEGAL_ARGUMENT&&entries==0);loaders[definitions[1].klass]=loader;
 Reset();live[definitions[1].klass]=incoming;assert(RunBatch(hooks,&env,&ti,definitions,provider,&error)==JVMTI_ERROR_UNSUPPORTED_REDEFINITION_SCHEMA_CHANGED&&entries==0);live[definitions[1].klass].methods.push_back({"removed","()I",1,0});
 Reset();Provider missing=provider;missing.preflight=nullptr;assert(RunBatch(hooks,&env,&ti,definitions,missing,&error)==JVMTI_ERROR_NOT_AVAILABLE&&entries==0);
 // A real parent/child DEX pair keeps its hierarchy unchanged. ART marks
 // the child actually-structural but only the parent's island initial.
 std::vector<Definition> tree;std::vector<evolution::ClassSchema> tree_schema;
 for(size_t n=0;n<2;n++){std::ifstream file(argv[2+n],std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});evolution::ClassSchema schema;assert(evolution::ReadDexDeclarations(bytes,&schema,&error));tree.push_back({reinterpret_cast<jclass>(heap.New(16)),bytes});tree_schema.push_back(schema);live[tree.back().klass]=schema;live[tree.back().klass].methods.push_back({"removed","()I",1,0});loaders[tree.back().klass]=reinterpret_cast<jobject>(heap.New(16));}
 assert(tree_schema[1].superclass==tree_schema[0].descriptor);
 Reset();Build(tree);descendant_mode=true;initial_bits=1;
 auto& parent=partitions[0].plan;const auto child_plan=partitions[1].plan;
 Put32(child_plan.old_classes[0],offsetof(class_metadata::Class17,super_class),uint32_t(uintptr_t(parent.old_classes[0])));
 Put32(child_plan.new_classes[0],offsetof(class_metadata::Class17,super_class),uint32_t(uintptr_t(parent.new_classes[0])));
 parent.old_classes.push_back(child_plan.old_classes[0]);parent.new_classes.push_back(child_plan.new_classes[0]);
 parent.old_instances.push_back(child_plan.old_instances[0]);parent.new_instances.push_back(child_plan.new_instances[0]);arrays[parent.new_classes_root].push_back(child_plan.new_classes[0]);
 assert(RunBatch(hooks,&env,&ti,tree,provider,&error)==JVMTI_ERROR_NONE&&begins==1&&prepared==1&&replacements==1&&original_commits==0);
 assert(Read32(child_plan.new_instances[0],8)==72&&Read32(child_plan.old_instances[0],8)==0);
 // The same native island may also contain a loaded descendant absent from
 // input definitions. A closed-tree provider still validates/migrates it.
 Reset();Build(tree);descendant_mode=true;initial_bits=1;holder.initial_structural.size=1;
 auto& parent_only=partitions[0].plan;const auto unlisted_child=partitions[1].plan;
 Put32(unlisted_child.old_classes[0],offsetof(class_metadata::Class17,super_class),uint32_t(uintptr_t(parent_only.old_classes[0])));
 Put32(unlisted_child.new_classes[0],offsetof(class_metadata::Class17,super_class),uint32_t(uintptr_t(parent_only.new_classes[0])));
 parent_only.old_classes.push_back(unlisted_child.old_classes[0]);parent_only.new_classes.push_back(unlisted_child.new_classes[0]);parent_only.old_instances.push_back(unlisted_child.old_instances[0]);parent_only.new_instances.push_back(unlisted_child.new_instances[0]);arrays[parent_only.new_classes_root].push_back(unlisted_child.new_classes[0]);
 partitions.resize(1);partitions[0].metadata.count=1;partitions[0].metadata.initial_structural=holder.initial_structural;
 std::vector<Definition> parent_definition{tree[0]};assert(RunBatch(hooks,&env,&ti,parent_definition,provider,&error)==JVMTI_ERROR_NONE&&prepared==1&&begins==1&&replacements==1);
 assert(Read32(unlisted_child.new_instances[0],8)==72&&Read32(unlisted_child.old_instances[0],8)==0);

 // Final readonly refusal is fatal ART invariant; no BeginCommit publication.
 Reset();Build(definitions);int pipes[2];assert(pipe(pipes)==0);pid_t child=fork();assert(child>=0);
 if(child==0){close(pipes[0]);publication_pipe=pipes[1];prepare_ok=false;std::freopen("/dev/null","w",stderr);(void)RunBatch(hooks,&env,&ti,definitions,provider,&error);_exit(99);}
 close(pipes[1]);int status=0;assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);char byte;assert(read(pipes[0],&byte,1)==0);close(pipes[0]);
 for(int mode=0;mode<2;mode++){Reset();Build(definitions);child=fork();assert(child>=0);if(child==0){skip_second=mode==0;repeat_second=mode==1;std::freopen("/dev/null","w",stderr);(void)RunBatch(hooks,&env,&ti,definitions,provider,&error);_exit(99);}assert(waitpid(child,&status,0)==child&&WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);}
 puts("Actual owned source TU: one extension, whole-plan readonly preparation, single publication/replacement, native-only exact callback consumption, namespace/provenance refusal and fatal invariant checks PASS");
 return 0;
}
