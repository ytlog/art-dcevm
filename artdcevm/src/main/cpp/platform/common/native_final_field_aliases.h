#pragma once
#include <memory>
#include <mutex>
#include <vector>
namespace artdcevm::final_field_aliases {
// Weak Class handles are owned by the JNI adapter; native metadata is retained
// by source ClassExt only while that replacement Class is live. Decode the weak
// first, before dereferencing the old ArtField's declaring-class root.
struct Alias {void* weak=nullptr;void* old_field=nullptr;};
struct Node {std::vector<Alias> aliases;Node* next=nullptr;};
struct Reader {
 void* owner=nullptr;
 bool (*weak)(void*,void*,void**)=nullptr;
 void* (*old_owner)(void*,void*)=nullptr;
};
class Set {
 public:
 explicit Set(Reader reader):reader_(reader){}~Set();
 Set(const Set&)=delete;Set& operator=(const Set&)=delete;
 // Consumes preallocated storage at the exclusive commit boundary; no JNI or
 // allocation. Native callback readers cannot outlive the process owner.
 void Publish(std::unique_ptr<Node>) noexcept;
 bool Equivalent(void* current_owner,void* historical_owner)const noexcept;
 // Ordinary JNI cleanup obtains handles before entering any JNI operation.
 std::vector<void*> WeakHandles()const;
 // Waits for current readers, prevents future decoding, then caller may safely
 // DeleteWeakGlobalRef outside this mutex. Frees fully inactive native nodes.
 bool Deactivate(void* weak)noexcept;
 private:
 Reader reader_;mutable std::mutex mutex_;Node* first_=nullptr;
};
} // namespace artdcevm::final_field_aliases
