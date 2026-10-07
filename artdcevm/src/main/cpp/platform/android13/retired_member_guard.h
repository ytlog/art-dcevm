#pragma once

namespace artdcevm::android13 {
// Fixed Android 13 thread.h declarations, called through named device symbols.
using CurrentGuardThread = void* (*)();
using ThrowGuardException = void (*)(void*, const char*, const char*);
using DeliverGuardException = void (*)(void*);
using ContainsRetiredMember = bool (*)(void*);
using RetiredMemberGuard = bool (*)(void*, void*, void*);

inline constexpr char kGuardCurrentThread[] = "_ZN3art6Thread14CurrentFromGdbEv";
inline constexpr char kGuardThrowNewException[] = "_ZN3art6Thread17ThrowNewExceptionEPKcS2_";
inline constexpr char kGuardQuickDeliverException[] = "_ZN3art6Thread21QuickDeliverExceptionEv";

struct RetiredMemberGuardFunctions {
  CurrentGuardThread current_thread;
  ThrowGuardException throw_exception;
  DeliverGuardException deliver_exception;
  ContainsRetiredMember contains_method;
  ContainsRetiredMember contains_field;
};

bool ConfigureRetiredMemberGuard(const RetiredMemberGuardFunctions& functions);
bool GuardRetiredMethod(void* method, void* thread, void* result);
bool GuardRetiredField(void* field, void* thread, void* result);
[[noreturn]] void DeliverRetiredMemberException(void* thread);
}  // namespace artdcevm::android13
