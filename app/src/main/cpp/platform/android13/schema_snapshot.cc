#include "platform/android13/schema_snapshot.h"
namespace artdcevm::android13 {
namespace {
template <typename T> class Allocation {
 public:
  explicit Allocation(jvmtiEnv* env) : env_(env) {}
  ~Allocation() { if (value) env_->Deallocate(reinterpret_cast<unsigned char*>(value)); }
  T* value = nullptr;
 private:
  jvmtiEnv* env_;
};
class LocalClass {
 public:
  LocalClass(JNIEnv* env, jclass value) : value(value), env_(env) {}
  ~LocalClass() { if (value) env_->DeleteLocalRef(value); }
  jclass value;
 private:
  JNIEnv* env_;
};
int Signature(jvmtiEnv* jvmti, jclass klass, std::string* output) {
  Allocation<char> name(jvmti);
  int result = jvmti->GetClassSignature(klass, &name.value, nullptr);
  if (result == JVMTI_ERROR_NONE) *output = name.value;
  return result;
}
}
int ReadLiveSchema(jvmtiEnv* jvmti, JNIEnv* jni, jclass klass,
                   evolution::ClassSchema* output, std::string* error) {
  auto fail = [&](int code, const char* operation) { *error = operation; return code; };
  evolution::ClassSchema schema;
  int result = Signature(jvmti, klass, &schema.descriptor);
  if (result) return fail(result, "GetClassSignature failed");
  jint access = 0;
  result = jvmti->GetClassModifiers(klass, &access);
  if (result) return fail(result, "GetClassModifiers failed");
  schema.access = static_cast<uint32_t>(access);
  LocalClass superclass(jni, jni->GetSuperclass(klass));
  if (jni->ExceptionCheck()) return fail(JVMTI_ERROR_INTERNAL, "GetSuperclass raised an exception");
  if (superclass.value) {
    result = Signature(jvmti, superclass.value, &schema.superclass);
    if (result) return fail(result, "Superclass signature failed");
  } else if (access & 0x0200) {
    // DEX encodes Object as the superclass of interfaces, whereas JNI returns null.
    schema.superclass = "Ljava/lang/Object;";
  }
  jint count = 0;
  Allocation<jclass> interfaces(jvmti);
  result = jvmti->GetImplementedInterfaces(klass, &count, &interfaces.value);
  if (result) return fail(result, "GetImplementedInterfaces failed");
  // Release every local interface reference even if a signature lookup fails midway.
  int interface_result = 0;
  for (jint i = 0; i < count; ++i) {
    LocalClass current(jni, interfaces.value[i]);
    if (interface_result) continue;
    std::string name;
    interface_result = Signature(jvmti, current.value, &name);
    if (!interface_result) schema.interfaces.push_back(std::move(name));
  }
  if (interface_result) return fail(interface_result, "Interface signature failed");
  Allocation<jfieldID> fields(jvmti);
  result = jvmti->GetClassFields(klass, &count, &fields.value);
  if (result) return fail(result, "GetClassFields failed");
  for (jint i = 0; i < count; ++i) {
    Allocation<char> name(jvmti), descriptor(jvmti);
    result = jvmti->GetFieldName(klass, fields.value[i], &name.value, &descriptor.value, nullptr);
    if (result) return fail(result, "GetFieldName failed");
    result = jvmti->GetFieldModifiers(klass, fields.value[i], &access);
    if (result) return fail(result, "GetFieldModifiers failed");
    schema.fields.push_back({name.value, descriptor.value, static_cast<uint32_t>(access), UINT32_MAX});
  }
  Allocation<jmethodID> methods(jvmti);
  result = jvmti->GetClassMethods(klass, &count, &methods.value);
  if (result) return fail(result, "GetClassMethods failed");
  for (jint i = 0; i < count; ++i) {
    Allocation<char> name(jvmti), descriptor(jvmti);
    result = jvmti->GetMethodName(methods.value[i], &name.value, &descriptor.value, nullptr);
    if (result) return fail(result, "GetMethodName failed");
    result = jvmti->GetMethodModifiers(methods.value[i], &access);
    if (result) return fail(result, "GetMethodModifiers failed");
    schema.methods.push_back({name.value, descriptor.value, static_cast<uint32_t>(access), UINT32_MAX});
  }
  *output = std::move(schema);
  error->clear();
  return JVMTI_ERROR_NONE;
}
}
