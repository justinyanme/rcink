#pragma once

#ifdef RCPAPER_DEV_CONSOLE

namespace rcpaper::dev {

// One-shot init for the side-button GPIOs the console polls in production-
// equivalent fashion (BtnA=next, BtnB=prev, BtnC=refetch). Idempotent.
void dev_buttons_init();

// Call from loop(). Polls the three buttons, debounces, and dispatches.
void dev_buttons_poll();

// Print the console command table on Serial.
void console_help();

// Single-character dispatcher. See console_help() for the full table.
void console_dispatch(char c);

}  // namespace rcpaper::dev

#endif  // RCPAPER_DEV_CONSOLE
