#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
namespace artdcevm::owned_active_frame {
// Native Thread* is retained only while its owner is blocked in native code.
// It is not a managed root or a Thread-object field, and release clears it.
class Gate {
 public:
  bool Arm(){std::lock_guard<std::mutex> lock(mutex_);if(armed_||target_)return false;armed_=true;released_=false;return true;}
  bool Await(void* native_thread){std::unique_lock<std::mutex> lock(mutex_);if(!armed_)return true;
    if(!native_thread||target_)return false;target_=native_thread;condition_.notify_all();
    condition_.wait(lock,[&]{return released_;});target_=nullptr;armed_=false;condition_.notify_all();return true;}
  bool Entered(unsigned milliseconds){std::unique_lock<std::mutex> lock(mutex_);return condition_.wait_for(lock,std::chrono::milliseconds(milliseconds),[&]{return target_!=nullptr;});}
  void* Target(){std::lock_guard<std::mutex> lock(mutex_);return armed_&&!released_?target_:nullptr;}
  void Release(){std::lock_guard<std::mutex> lock(mutex_);released_=true;if(!target_)armed_=false;condition_.notify_all();}
 private:std::mutex mutex_;std::condition_variable condition_;bool armed_=false,released_=false;void* target_=nullptr;
};
}
