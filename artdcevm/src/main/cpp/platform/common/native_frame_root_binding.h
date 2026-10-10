#pragma once
namespace artdcevm::whole_process_frames {
class FrameRootBinding;
// Only the concrete whole-thread frame owner issues this synchronous binding.
// No caller-supplied callback can claim source frame recapture succeeded.
class RecaptureInterface {
 public:
  virtual ~RecaptureInterface()=default;
  RecaptureInterface(const RecaptureInterface&)=delete;
  RecaptureInterface&operator=(const RecaptureInterface&)=delete;
  RecaptureInterface(RecaptureInterface&&)=delete;
  RecaptureInterface&operator=(RecaptureInterface&&)=delete;

  virtual bool Recapture(bool after)const noexcept=0;
 private:
  friend class FrameRootBinding;
  RecaptureInterface()=default;

};
}
