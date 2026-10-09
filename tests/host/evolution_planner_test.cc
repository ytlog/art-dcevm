#include "platform/common/evolution_planner.h"
#include "evolution/class_schema.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace artdcevm;
namespace {
evolution::ClassSchema live;
jclass owner = reinterpret_cast<jclass>(uintptr_t{0x1000});
jclass super = reinterpret_cast<jclass>(uintptr_t{0x2000});
int allocations = 0, reads = 0;
bool fail_signature = false;
void Check(bool condition, const char* text) {if (!condition) throw std::runtime_error(text);}
char* Copy(const std::string& value) {
  auto* p = static_cast<char*>(std::malloc(value.size() + 1));
  Check(p != nullptr, "allocation failed"); ++allocations;
  std::memcpy(p, value.c_str(), value.size() + 1); return p;
}
jvmtiError JNICALL Deallocate(jvmtiEnv*, unsigned char* p) {if (p) {--allocations;std::free(p);}return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Signature(jvmtiEnv*, jclass c, char** out, char**) {
  ++reads;if (fail_signature) return JVMTI_ERROR_INVALID_CLASS;
  *out = Copy(c == super ? live.superclass : live.descriptor);return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL ClassModifiers(jvmtiEnv*, jclass, jint* out) {*out = static_cast<jint>(live.access);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL Interfaces(jvmtiEnv*, jclass, jint* count, jclass** out) {*count = 0;*out = nullptr;return JVMTI_ERROR_NONE;}
template<class T> jvmtiError Members(const std::vector<evolution::Member>& members,jint* count,T** out) {
  *count = static_cast<jint>(members.size());*out = nullptr;
  if (!members.empty()) {
    *out = static_cast<T*>(std::malloc(sizeof(T) * members.size()));Check(*out != nullptr,"member allocation");++allocations;
    for (size_t i = 0;i < members.size();++i) (*out)[i] = reinterpret_cast<T>(i + 1);
  }return JVMTI_ERROR_NONE;
}
jvmtiError JNICALL Fields(jvmtiEnv*, jclass, jint* n, jfieldID** out) {return Members(live.fields,n,out);}
jvmtiError JNICALL Methods(jvmtiEnv*, jclass, jint* n, jmethodID** out) {return Members(live.methods,n,out);}
size_t Index(const void* id) {return reinterpret_cast<uintptr_t>(id) - 1;}
jvmtiError Name(const evolution::Member& member,char** name,char** signature) {*name = Copy(member.name);*signature = Copy(member.descriptor);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL FieldName(jvmtiEnv*, jclass, jfieldID id,char** name,char** signature,char**) {return Name(live.fields.at(Index(id)),name,signature);}
jvmtiError JNICALL MethodName(jvmtiEnv*, jmethodID id,char** name,char** signature,char**) {return Name(live.methods.at(Index(id)),name,signature);}
jvmtiError JNICALL FieldModifiers(jvmtiEnv*, jclass, jfieldID id,jint* out) {*out = static_cast<jint>(live.fields.at(Index(id)).access);return JVMTI_ERROR_NONE;}
jvmtiError JNICALL MethodModifiers(jvmtiEnv*, jmethodID id,jint* out) {*out = static_cast<jint>(live.methods.at(Index(id)).access);return JVMTI_ERROR_NONE;}
jboolean JNICALL Same(JNIEnv*, jobject a,jobject b) {return a == b || (a == owner && b == reinterpret_cast<jobject>(uintptr_t{0x3000})) || (b == owner && a == reinterpret_cast<jobject>(uintptr_t{0x3000}));}
jclass JNICALL Super(JNIEnv*, jclass) {return live.superclass.empty() ? nullptr : super;}
jboolean JNICALL Exception(JNIEnv*) {return JNI_FALSE;}
void JNICALL Delete(JNIEnv*, jobject) {}
std::vector<unsigned char> Read(const std::string& path) {std::ifstream f(path,std::ios::binary);Check(f.good(),"missing generated dex fixture");return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
}
int main(int argc,char** argv) {
 try {
  Check(argc == 2,"provide assets");
  const std::string suffix="/com/github/ytlog/artdcevm/fixture/Evolving.dex";
  const auto old_bytes=Read(std::string(argv[1])+"/body"+suffix), add_bytes=Read(std::string(argv[1])+"/add"+suffix);
  std::string error;Check(evolution::ReadDexSchema(old_bytes,&live,&error),"parse live fixture");
  JNINativeInterface_ jni_table{};jni_table.IsSameObject=Same;jni_table.GetSuperclass=Super;jni_table.ExceptionCheck=Exception;jni_table.DeleteLocalRef=Delete;JNIEnv jni{&jni_table};
  jvmtiInterface_1_ table{};table.GetClassSignature=Signature;table.GetClassModifiers=ClassModifiers;table.GetImplementedInterfaces=Interfaces;table.GetClassFields=Fields;table.GetClassMethods=Methods;table.GetFieldName=FieldName;table.GetMethodName=MethodName;table.GetFieldModifiers=FieldModifiers;table.GetMethodModifiers=MethodModifiers;table.Deallocate=Deallocate;jvmtiEnv ti{&table};
  evolution_planner::BatchPlan output;
  Check(evolution_planner::PlanEvolution(&ti,&jni,{{owner,add_bytes}},&output,&error)==JVMTI_ERROR_NONE,"actual additive planner");
  Check(output.classes.size()==1 && output.required_features==(evolution::kAddField|evolution::kAddMethod),"additive requirements");
  Check(output.classes[0].before.fields.size()==live.fields.size(),"actual public snapshot field read");
  Check(evolution_planner::DescribeBatchPlan(output).find("requiredFeatures")!=std::string::npos,"report");
  Check(evolution_planner::PlanEvolution(&ti,&jni,{{owner,old_bytes},{reinterpret_cast<jclass>(uintptr_t{0x4000}),add_bytes}},&output,&error)==JVMTI_ERROR_NONE,"distinct live classes batch");
  Check(output.classes.size()==2 && output.classes[0].required_features==0 && output.required_features==(evolution::kAddField|evolution::kAddMethod),"batch requirements union");
  Check(evolution_planner::PlanEvolution(&ti,&jni,{{owner,add_bytes}},&output,&error)==JVMTI_ERROR_NONE,"reset preserved successful plan");
  const auto preserved=output.required_features;
  Check(evolution_planner::PlanEvolution(&ti,&jni,{},&output,&error)==JVMTI_ERROR_ILLEGAL_ARGUMENT,"empty rejection");
  Check(output.required_features==preserved,"empty preserves plan");
  Check(evolution_planner::PlanEvolution(&ti,&jni,{{owner,add_bytes},{reinterpret_cast<jclass>(uintptr_t{0x3000}),add_bytes}},&output,&error)==JVMTI_ERROR_ILLEGAL_ARGUMENT,"duplicate JNI identity rejection");
  Check(output.classes.size()==1 && output.required_features==preserved,"duplicate preserves output");
  Check(evolution_planner::PlanEvolution(&ti,&jni,{{owner,{1,2,3}}},&output,&error)==JVMTI_ERROR_INVALID_CLASS_FORMAT,"malformed dex rejection");
  auto saved=live.descriptor;live.descriptor="Lwrong/Target;";
  Check(evolution_planner::PlanEvolution(&ti,&jni,{{owner,add_bytes}},&output,&error)==JVMTI_ERROR_NAMES_DONT_MATCH,"name mismatch");live.descriptor=saved;
  fail_signature=true;
  Check(evolution_planner::PlanEvolution(&ti,&jni,{{owner,add_bytes}},&output,&error)==JVMTI_ERROR_INVALID_CLASS,"public metadata failure");fail_signature=false;
  Check(output.classes.size()==1 && output.required_features==preserved,"metadata failure preserves output");
  Check(allocations==0 && reads>0,"public allocations released");
  Check(evolution_planner::PlanEvolution(&ti,&jni,{{owner,old_bytes}},&output,&error)==JVMTI_ERROR_NONE && output.required_features==0,"body-only unchanged schema");
  Check(allocations==0,"all public allocations released");
  std::cout<<"Common batch planner + actual DEX parser, migration and public schema snapshot: PASS\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
