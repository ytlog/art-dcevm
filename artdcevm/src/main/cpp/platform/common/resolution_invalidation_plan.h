#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
namespace artdcevm::resolution_invalidation {
enum class Kind { Method, Field };
enum class Decision { Keep, Clear, Unavailable };
struct ClassIdentity {void* klass=nullptr;void* loader=nullptr;void* parent=nullptr;std::string descriptor;};
struct MemberIdentity {void* member=nullptr;void* declaring=nullptr;uint32_t flags=0;};
struct FailureInfo {
 uint32_t source_kind=0;Kind kind=Kind::Method;void* member=nullptr;void* declaring=nullptr;uint32_t flags=0;
 const char* stage=nullptr;const char* observer_reason=nullptr;
};
struct Declaration : MemberIdentity {Kind kind=Kind::Field;std::string name,descriptor;};
struct Replacement {ClassIdentity before,after;std::vector<Declaration> declarations;};
// The observer is owned by the immutable plan. Its native implementation uses
// calibrated declarations and named ART identities inside the original rooted,
// disabled-GC transaction; callers cannot substitute descriptor-only ancestry.
class Observer {
 public:
  virtual ~Observer()=default;
  // Eligibility needs flags only. It must not decode a declaring root, invoke
  // GetDescriptor, or inspect a class field table for excluded cache entries.
  virtual bool ReadFlags(void* member,Kind kind,uint32_t* flags)const {
    MemberIdentity identity;if(!flags||!ReadMember(member,kind,&identity))return false;*flags=identity.flags;return true;
  }
  virtual bool ValidateScope(void*)const{return true;}
  virtual const char* FailureReason()const{return nullptr;}
  virtual bool ReadClass(void*,ClassIdentity*)const=0;
  virtual bool ReadMember(void*,Kind,MemberIdentity*)const=0;
  virtual bool IsAssignableFrom(void* target,void* holder,bool*)const=0;
  virtual bool SameIdentity(const MemberIdentity&,const Declaration&,bool*)const=0;
};
class ResolutionInvalidationPlan {
 public:
  static std::unique_ptr<ResolutionInvalidationPlan> Prepare(void* holder_mirror,
      const std::vector<Replacement>&,std::shared_ptr<const Observer>,std::string*);
  Decision Inspect(void* member,Kind,uint32_t source_kind,FailureInfo* failure=nullptr)const;
  // Only one current original exclusive/noGC visitor traversal. All root/class/
  // interface proofs are renewed on creation; this object cannot cross GC.
  class Scan final {
   public:
    explicit Scan(const ResolutionInvalidationPlan&);
    Decision Inspect(void*,Kind,uint32_t,FailureInfo* failure=nullptr);
    bool Valid()const{return valid_;}
   private:
    const ResolutionInvalidationPlan& plan_;bool valid_=false;
    struct Result{Decision decision;FailureInfo failure;};
    std::unordered_map<void*,Result> methods_,fields_;
    std::unordered_map<void*,bool> assignability_;
  };
 private:
  bool ValidateScope()const;
  Decision InspectImpl(void*,Kind,uint32_t,FailureInfo*,bool,
      std::unordered_map<void*,bool>* assignability=nullptr)const;
  void* holder_=nullptr;
  std::shared_ptr<const Observer> observer_;
  std::vector<void*> eligible_classes_;
  std::vector<ClassIdentity> classes_;
  std::vector<Declaration> declarations_;
};
}
