// Source calibration accessors still use the device GC read barrier.
namespace art::mirror { class Object; }
namespace {
using ReadBarrierMark = art::mirror::Object* (*)(art::mirror::Object*);
ReadBarrierMark device_read_barrier_mark = nullptr;
}
extern "C" void ArtDcevmSourceConfigureReadBarrier(void* function) {
  device_read_barrier_mark = reinterpret_cast<ReadBarrierMark>(function);
}
extern "C" art::mirror::Object* ArtDcevmSourceReadBarrierMark(art::mirror::Object* object) {
  return device_read_barrier_mark(object);
}
