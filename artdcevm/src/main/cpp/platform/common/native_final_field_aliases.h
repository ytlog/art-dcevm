#pragma once
#include <memory>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>
namespace artdcevm::final_field_aliases {
// Weak Class handles are owned by the JNI adapter; native metadata is retained
// by source ClassExt only while that replacement Class is live. Decode the weak
// first, before dereferencing a retained native member's declaring-class root.
// A method anchor also supports old Classes with no fields when final fields
// are introduced. The source retains the native member through ClassExt.
enum class MemberKind:uint8_t {Field,Method};
struct Alias {void* weak=nullptr;void* member=nullptr;void* retiring=nullptr;size_t readers=0;MemberKind kind=MemberKind::Field;};
struct Node {std::vector<Alias> aliases;Node* next=nullptr;uint64_t sequence=0;};
struct Reader {
 void* owner=nullptr;
 bool (*weak)(void*,void*,void**)=nullptr;
 bool (*weak_thread)(void*,void* thread,void* weak,void**)=nullptr;
 void* (*field_owner)(void*,void*)=nullptr;
 void* (*method_owner)(void*,void*)=nullptr;
};
class Set {
 public:
 explicit Set(Reader reader):reader_(reader){}~Set();
 Set(const Set&)=delete;Set& operator=(const Set&)=delete;
 // Consumes preallocated storage at the exclusive commit boundary; no JNI or
 // allocation. Native callback readers cannot outlive the process owner.
 void Publish(std::unique_ptr<Node>) noexcept;
 // Class-pointer variant requires a nonsuspending reader; production uses
 // native anchors below when weak decoding can suspend.
 bool Equivalent(void* current_owner,void* historical_owner)const noexcept;
 bool EquivalentWriter(void* thread,void* native_field,void* native_method)const noexcept;
 // Ordinary JNI cleanup obtains handles before entering any JNI operation.
 std::vector<void*> WeakHandles()const;
 // Waits for current readers, prevents future decoding, then caller may safely
 // DeleteWeakGlobalRef outside this mutex. Frees fully inactive native nodes.
 bool Deactivate(void* weak)noexcept;
 private:
 struct Lease{Alias* alias=nullptr;void* weak=nullptr;void* member=nullptr;MemberKind kind=MemberKind::Field;};
 void* HistoricalOwner(const Lease&)const noexcept;
 bool Next(uint64_t*,size_t*,Lease*)const noexcept;
 void Release(const Lease&)const noexcept;
 Reader reader_;mutable std::mutex mutex_;mutable std::condition_variable readers_;
 Node* first_=nullptr;uint64_t sequence_=0;
};
} // namespace artdcevm::final_field_aliases
