#include "platform/common/owned_active_frame_gate.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
using artdcevm::owned_active_frame::Gate;
int main(){Gate gate;int target,other;
 assert(gate.Await(nullptr));assert(!gate.Entered(1));assert(!gate.Target());
 assert(gate.Arm());assert(!gate.Arm());assert(!gate.Await(nullptr));
 std::atomic<bool> returned{false};std::thread worker([&]{assert(gate.Await(&target));returned=true;});
 assert(gate.Entered(2000));assert(gate.Target()==&target&&!returned);assert(!gate.Await(&other));
 gate.Release();worker.join();assert(returned&&!gate.Target());
 assert(gate.Arm());gate.Release();assert(!gate.Target());assert(gate.Await(&target));
 assert(gate.Arm());std::thread second([&]{assert(gate.Await(&other));});
 assert(gate.Entered(2000));assert(gate.Target()==&other);gate.Release();second.join();assert(!gate.Target());
 puts("Owned native gate: actual blocked worker identity, duplicate owner rejection, release/cancel and pointer lifetime passed");}
