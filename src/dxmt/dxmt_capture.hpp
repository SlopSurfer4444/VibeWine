#pragma once
#include <cstdint>

namespace dxmt {

class CaptureState {
public:
  enum class NextAction {
    StartCapture,
    StopCapture,
    Nothing,
  };
  NextAction getNextAction(uint64_t frame);
  void scheduleNextFrameCapture(uint64_t next = 0);
  bool shouldCaptureNextFrame();
  void captureStarted(bool success);
  bool takeCaptureOwnership();

  CaptureState();

private:
  enum class State {
    Freezed,
    Idle,
    WillCapture,
    Capturing,
  };
  State state = State::Freezed;
  uint64_t next_capture_frame = 0;
  bool capture_key_pressed_ = false;
  bool owns_capture_ = false;
};

} // namespace dxmt
