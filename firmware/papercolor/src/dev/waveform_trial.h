#pragma once

namespace rcpaper::dev {

// One bounded test chart. F/Q: original TICKEY staged color; M/C: Mono/MonoClear;
// N: native OTP reference; I: one FreeInk340ms interrupted native mono drive.
// No persistent automatic mode or framebuffer history.
// Returns only command/BUSY success; optical acceptance needs the camera.
bool waveform_trial(char mode);

}  // namespace rcpaper::dev
