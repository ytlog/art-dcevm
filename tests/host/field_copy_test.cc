#include "platform/android13/field_copy.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
// Fail immediately if the commit scheduler allocates C++ storage. Mocks also
// use fixed event/value arrays while this guard is active.
bool forbid_allocation = false;
void* operator new(size_t size) {
  if (forbid_allocation) std::abort();
  if (auto* result = std::malloc(size ? size : 1)) return result;
  throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
using namespace artdcevm::android13;
namespace {
struct Field { const char* name; const char* type; size_t slot; bool is_volatile = false; };
struct Class { const char* descriptor; void* loader; Class* parent; std::vector<Field> instance, statics; bool object = false; };
struct Slot {
  std::atomic<uint8_t> byte{0}; std::atomic<uint16_t> short_value{0};
  std::atomic<uint32_t> word{0}; std::atomic<uint64_t> wide{0}; std::atomic<void*> reference{nullptr};
};
struct Object { std::array<Slot, 64> slots; uint32_t monitor = 0; };
struct Event { char kind; Object* object; Field* field; uint64_t value; };
std::array<Event, 512> events;
size_t event_count = 0;
void Log(char kind, Object* object, Field* field = nullptr, uint64_t value = 0) {
  assert(event_count < events.size()); events[event_count++] = {kind, object, field, value};
}
CopyClassView Describe(void* raw, bool is_static) {
  auto* klass = static_cast<Class*>(raw);
  return {klass->parent, klass->loader, klass->descriptor,
          (is_static ? klass->statics : klass->instance).size(), klass->object};
}
CopyFieldView DescribeField(void* raw, bool is_static, size_t index) {
  auto* klass = static_cast<Class*>(raw);
  auto* field = &(is_static ? klass->statics : klass->instance).at(index);
  return {field, field->name, field->type};
}
uint64_t Load(Object* object, Field* field, char type) {
  auto& slot = object->slots.at(field->slot);
  auto order = field->is_volatile ? std::memory_order_seq_cst : std::memory_order_relaxed;
  switch (type) {
    case 'Z': case 'B': return slot.byte.load(order);
    case 'C': case 'S': return slot.short_value.load(order);
    case 'I': case 'F': return slot.word.load(order);
    case 'J': case 'D': return slot.wide.load(order);
    case 'L': case '[': return reinterpret_cast<uintptr_t>(slot.reference.load(order));
    default: std::abort();
  }
}
void Store(Object* object, Field* field, char type, uint64_t value) {
  auto& slot = object->slots.at(field->slot);
  auto order = field->is_volatile ? std::memory_order_seq_cst : std::memory_order_relaxed;
  switch (type) {
    case 'Z': case 'B': slot.byte.store(static_cast<uint8_t>(value), order); return;
    case 'C': case 'S': slot.short_value.store(static_cast<uint16_t>(value), order); return;
    case 'I': case 'F': slot.word.store(static_cast<uint32_t>(value), order); return;
    case 'J': case 'D': slot.wide.store(value, order); return;
    case 'L': case '[': slot.reference.store(reinterpret_cast<void*>(static_cast<uintptr_t>(value)), order); return;
    default: std::abort();
  }
}
uint64_t Read(void* raw_object, void* raw_field, char type) {
  auto* object = static_cast<Object*>(raw_object); auto* field = static_cast<Field*>(raw_field);
  assert(type == field->type[0]); auto value = Load(object, field, type);
  Log('r', object, field, value); return value;
}
void Write(void* raw_object, void* raw_field, char type, uint64_t value) {
  auto* object = static_cast<Object*>(raw_object); auto* field = static_cast<Field*>(raw_field);
  assert(type == field->type[0]); Store(object, field, type, value); Log('w', object, field, value);
}
void Monitor(void* raw_target, void* raw_source) {
  auto* target = static_cast<Object*>(raw_target); auto* source = static_cast<Object*>(raw_source);
  target->monitor = source->monitor; source->monitor = 0; Log('m', target);
}
void Barrier(void* object) { Log('b', static_cast<Object*>(object)); }
FieldCopyAccess ops{Describe, DescribeField, Read, Write, Monitor, Barrier};
void Transfer(bool is_static, Object* target, Class* target_class, Object* source, Class* source_class) {
  forbid_allocation = true;
  CopyAndClearEvolutionFields(ops, is_static, target, target_class, source, source_class);
  forbid_allocation = false;
}

void Put(Object* object, Class* klass, bool statics, size_t index, uint64_t value) {
  auto* field = &(statics ? klass->statics : klass->instance).at(index);
  Store(object, field, field->type[0], value);
}
uint64_t Get(Object* object, Class* klass, bool statics, size_t index) {
  auto* field = &(statics ? klass->statics : klass->instance).at(index);
  return Load(object, field, field->type[0]);
}
void Order(Object* target, Object* source) {
  size_t monitor = 0;
  while (monitor < event_count && events[monitor].kind != 'm') {
    assert(events[monitor].kind == 'r' || (events[monitor].kind == 'w' && events[monitor].object == target));
    ++monitor;
  }
  assert(monitor < event_count && events[monitor].object == target);
  assert(events[monitor + 1].kind == 'b' && events[monitor + 1].object == target);
  for (size_t i = monitor + 2; i + 1 < event_count; ++i)
    assert(events[i].kind == 'w' && events[i].object == source && events[i].value == 0);
  assert(events[event_count - 1].kind == 'b' && events[event_count - 1].object == source);
}
}
int main() {
  void* loader = reinterpret_cast<void*>(1);
  Class root{"Ljava/lang/Object;", nullptr, nullptr, {}, {}, true};
  Class before_parent{"LParent;", loader, &root, {{"same", "I", 20}, {"wide", "J", 21, true}}, {}};
  Class after_parent{"LParent;", loader, &root, {{"same", "I", 40}, {"wide", "J", 41}}, {}};
  Class middle{"LInserted;", loader, &after_parent, {{"same", "I", 42}, {"fresh", "Ljava/lang/Object;", 43}}, {}};
  Class before{"LChild;", loader, &before_parent,
      {{"z", "Z", 0}, {"b", "B", 1}, {"c", "C", 2}, {"s", "S", 3},
       {"i", "I", 4}, {"f", "F", 5}, {"j", "J", 6, true}, {"d", "D", 7},
       {"ref", "Ljava/lang/Object;", 8, true}, {"array", "[[I", 9}, {"same", "I", 10},
       {"deleted", "I", 11}, {"type", "I", 12}, {"storage", "I", 13}},
      {{"static", "J", 14, true}, {"staticRemoved", "I", 15}, {"instanceNow", "I", 16}}};
  Class after{"LChild;", loader, &middle,
      {{"same", "I", 30}, {"array", "[[I", 29, true}, {"ref", "Ljava/lang/Object;", 28},
       {"d", "D", 27, true}, {"j", "J", 26}, {"f", "F", 25}, {"i", "I", 24, true},
       {"s", "S", 23}, {"c", "C", 22}, {"b", "B", 19}, {"z", "Z", 18},
       {"fresh", "I", 31}, {"type", "J", 32}, {"instanceNow", "I", 33}},
      {{"static", "J", 34}, {"storage", "I", 35}}};
  Object source, target;
  const std::array<uint64_t, 14> values{1, 0xff, 0xffee, 0xff80, 0xfedcba98,
      0x7fc01234, 0x8877665544332211, 0x7ff8123456789012, 0x123450, 0x678900,
      111, 999, 777, 555};
  for (size_t i = 0; i < values.size(); ++i) Put(&source, &before, false, i, values[i]);
  Put(&source, &before_parent, false, 0, 222); Put(&source, &before_parent, false, 1, 0xaabbccdd00112233);
  source.monitor = 0x98765432;
  Transfer(false, &target, &after, &source, &before);
  Order(&target, &source); assert(target.monitor == 0x98765432 && source.monitor == 0);
  for (size_t i = 0; i < 11; ++i) assert(Get(&target, &after, false, 10 - i) == values[i]);
  assert(Get(&target, &after, false, 11) == 0 && Get(&target, &after, false, 12) == 0);
  assert(Get(&target, &after, false, 13) == 0);  // static -> instance initializes.
  assert(Get(&target, &after_parent, false, 0) == 222);
  assert(Get(&target, &after_parent, false, 1) == 0xaabbccdd00112233);
  assert(Get(&target, &middle, false, 0) == 0 && Get(&target, &middle, false, 1) == 0);
  for (size_t i = 0; i < before.instance.size(); ++i) assert(Get(&source, &before, false, i) == 0);
  assert(Get(&source, &before_parent, false, 0) == 0 && Get(&source, &before_parent, false, 1) == 0);
  // Static copying is confined to the declaring class and ignores instance
  // replacements and parent statics. Missing/type/storage fields only clear.
  event_count = 0;
  Object static_source, static_target;
  Put(&static_source, &before, true, 0, 0x123456789abcdef0);
  Put(&static_source, &before, true, 1, 123); Put(&static_source, &before, true, 2, 456);
  static_source.monitor = 1234;
  Transfer(true, &static_target, &after, &static_source, &before);
  Order(&static_target, &static_source);
  assert(Get(&static_target, &after, true, 0) == 0x123456789abcdef0);
  assert(Get(&static_target, &after, true, 1) == 0); // instance -> static initializes.
  for (size_t i = 0; i < before.statics.size(); ++i) assert(Get(&static_source, &before, true, i) == 0);
  // Identical descriptors from different loaders do not share storage values.
  event_count = 0; after_parent.loader = reinterpret_cast<void*>(2);
  Put(&source, &before_parent, false, 0, 345); Put(&target, &after_parent, false, 0, 0);
  Transfer(false, &target, &after, &source, &before);
  assert(Get(&target, &after_parent, false, 0) == 0 && Get(&source, &before_parent, false, 0) == 0);
  Order(&target, &source);
  // Removing the old parent entirely also skips copies and clears old fields.
  event_count = 0; after.parent = &root;
  Put(&source, &before_parent, false, 0, 567);
  Transfer(false, &target, &after, &source, &before);
  assert(Get(&source, &before_parent, false, 0) == 0); Order(&target, &source);
  std::cout << "field copying: all primitive/reference/array values, volatile/wide bits, declaring identity, inserted/removed parents, hiding, type/storage transitions, monitor/barrier ordering and no C++ allocations passed\n";
}
