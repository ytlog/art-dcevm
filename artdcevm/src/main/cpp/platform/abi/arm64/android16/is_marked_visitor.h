#pragma once
// Exact ed6c006bd06ae060bd9698fd2cb25c4865512ec3 runtime/object_callbacks.h.
// Verified 2026-10-11 from the same-commit LineageOS AOSP mirror:
// https://raw.githubusercontent.com/LineageOS/android_art/ed6c006bd06ae060bd9698fd2cb25c4865512ec3/runtime/object_callbacks.h
namespace art {
namespace mirror { class Object; }
class IsMarkedVisitor {
 public:
  virtual ~IsMarkedVisitor() {}
  virtual mirror::Object* IsMarked(mirror::Object* obj) = 0;
};
}
