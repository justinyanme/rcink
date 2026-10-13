#include "waveform_trial.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <lgfx/v1/Bus.hpp>

// Original, unmodified sf32rc/v2/src generated arrays. TICKEY2.1.0 firmware:
// SHA256 519fe0e3c585db7d4e7f84870ba44c12b4bef3daf145f23bb4d9fea4811c82c3.
// Host RAM LUT experiments only: no OTP writes, register scans or voltage tuning.
#define TICKEY_WAVEFORM_BYTES 799u
#include "tickey_waveforms_mono.inc"
#include "tickey_waveforms_color.inc"

namespace rcpaper::dev {
namespace {

constexpr unsigned kWidth = 400, kHeight = 600;
constexpr int kReset = 12;  // Physical PaperColor RST; pinned cfg.pin_rst is DC43.
constexpr uint32_t kBusyAssertMs = 1000, kBusyReleaseMs = 25000;
struct PaletteEntry { uint8_t code, r, g, b; };
#include "tickey_trial_palette.inc"
constexpr PaletteEntry kNative[] = {
    {0, 0, 0, 0}, {1, 255, 255, 255}, {2, 255, 243, 56},
    {3, 191, 0, 0}, {5, 100, 64, 255}, {6, 67, 138, 28},
};

struct Trial {
  lgfx::IBus* bus;
  int cs, busy;
  uint32_t deadline;

  bool budget(const char* stage) {
    if (int32_t(millis() - deadline) < 0) return true;
    Serial.printf("[trial] FAILED stage=%s overall-deadline\n", stage);
    return false;
  }

  bool pause(uint32_t ms) {
    if (!budget("delay")) return false;
    delay(ms);
    return budget("delay");
  }

  bool level(int wanted, uint32_t timeout, const char* stage) {
    if (!budget(stage)) return false;
    const uint32_t began = millis();
    while (digitalRead(busy) != wanted) {
      if (millis() - began >= timeout || int32_t(millis() - deadline) >= 0) {
        Serial.printf("[trial] FAILED stage=%s busy=%d wanted=%d timeout_ms=%lu\n",
                      stage, digitalRead(busy), wanted, (unsigned long)timeout);
        return false;
      }
      delay(1);
    }
    return true;
  }

  bool command(uint8_t cmd, const uint8_t* bytes = nullptr, unsigned count = 0) {
    if (!budget("command")) return false;
    bus->beginTransaction();
    digitalWrite(cs, LOW);
    const bool ok = bus->writeCommand(cmd, 8);
    if (ok && count) bus->writeBytes(bytes, count, true, false);
    bus->wait();
    digitalWrite(cs, HIGH);
    bus->endTransaction();
    if (!ok) Serial.printf("[trial] FAILED command=0x%02x\n", cmd);
    return ok;
  }

  bool cycle(uint8_t cmd, const char* stage) {
    const uint32_t began = millis();
    const uint8_t zero = 0;
    if (!level(HIGH, kBusyReleaseMs, "pre-command-idle")) return false;
    if (!command(cmd, &zero, cmd == 0x04 ? 0 : 1)) return false;
    const bool ok = level(LOW, kBusyAssertMs, stage) && level(HIGH, kBusyReleaseMs, stage);
    Serial.printf("[trial] phase=%s drive_ms=%lu result=%d\n",
                  stage, (unsigned long)(millis() - began), ok ? 0 : -1);
    return ok && (cmd != 0x04 || pause(10));
  }

  bool board_power_on(const char* stage) {
    // PaperColor pinned Panel_ED2208::_turn_on_display second booster setting.
    // Same existing board values for native and custom LUT drives; no sweep.
    static constexpr uint8_t boost[] = {0x6f, 0x1f, 0x17, 0x27};
    const bool ok = cycle(0x04, stage) && pause(190)  // cycle's10ms +190ms =200ms.
        && command(0x06, boost, sizeof(boost)) && pause(200);
    if (ok) Serial.println("[trial] booster=papercolor command=06 data=6F,1F,17,27");
    return ok;
  }

  bool interrupt_native() {
    // FreeInk e91e34e Ed2208M5Driver.cpp:238–257: cutoff after observed BUSY
    // assertion, never after command send. Defaultlight mapping is white1/black0.
    const uint8_t zero = 0;
    if (!level(HIGH, kBusyReleaseMs, "interrupt-pre-idle") || !command(0x12, &zero, 1)
        || !level(LOW, kBusyAssertMs, "interrupt-assert")) return false;
    const uint32_t asserted = millis();
    delay(340);
    digitalWrite(kReset, LOW);  // Cut drive before logging; finish() restores native.
    Serial.printf("[trial] phase=interrupted-native cutoff_ms=%lu anchor=observed-BUSY-assert polarity=white1-black0\n",
                  (unsigned long)(millis() - asserted));
    return budget("interrupt-finished");
  }

  bool off(const char* stage) {
    const uint32_t began = millis();
    const uint8_t zero = 0;
    if (!level(HIGH, kBusyReleaseMs, "pre-off-idle") || !command(0x02, &zero, 1)) return false;
    while (digitalRead(busy) == HIGH && millis() - began < 20) {
      if (!budget(stage)) return false;
      delay(1);
    }
    const bool asserted = digitalRead(busy) == LOW;
    const bool ok = level(HIGH, kBusyReleaseMs, stage) && pause(20);
    Serial.printf("[trial] phase=%s drive_ms=%lu busy_asserted=%d result=%d\n",
                  stage, (unsigned long)(millis() - began), asserted, ok ? 0 : -1);
    return ok;
  }

  bool native_init() {
    // Exact pinned Panel_ED2208.hpp getInitCommands; preserve board analog values.
    static constexpr uint8_t init[] = {
      0xaa, 6, 0x49, 0x55, 0x20, 0x08, 0x09, 0x18,
      0x01, 1, 0x3f, 0x00, 2, 0x5f, 0x69,
      0x05, 4, 0x40, 0x1f, 0x1f, 0x2c,
      0x08, 4, 0x6f, 0x1f, 0x1f, 0x22,
      0x06, 4, 0x6f, 0x1f, 0x17, 0x17,
      0x03, 4, 0x03, 0x54, 0x00, 0x44,
      0x60, 2, 0x02, 0x00, 0x30, 1, 0x08, 0x50, 1, 0x3f,
      0xe3, 1, 0x2f, 0x84, 1, 0x01,
      0x61, 4, 0x01, 0x90, 0x02, 0x58,
    };
    pinMode(kReset, OUTPUT);
    digitalWrite(kReset, LOW);
    if (!pause(20)) { digitalWrite(kReset, HIGH); return false; }
    digitalWrite(kReset, HIGH);
    if (!pause(20)) return false;
    for (unsigned i = 0; i < sizeof(init);) {
      if (!level(HIGH, kBusyReleaseMs, "native-init-idle")) return false;
      const uint8_t cmd = init[i++], count = init[i++];
      if (!command(cmd, init + i, count)) return false;
      i += count;
    }
    return level(HIGH, kBusyReleaseMs, "native-init-finished");
  }

  bool lut(const uint8_t* bytes) {
    static constexpr uint8_t psr[] = {0x0f, 0xa9};
    return level(HIGH, kBusyReleaseMs, "lut-idle")
        && command(0x00, psr, sizeof(psr))
        && command(0x20, bytes, TICKEY_WAVEFORM_BYTES);
  }

  bool configure_custom() {
    // Exact sf32rc display.c configure_trial(), decoded TICKEY2.1.0 setup.
    // Volatile source values only; field meanings/this panel's fit remain unknown.
    static constexpr uint8_t setup[][2] = {
      {0x30, 0x04}, {0x41, 0x00}, {0xe0, 0x02}, {0xe6, 0x19},
    };
    for (const auto& pair : setup) {
      if (!level(HIGH, kBusyReleaseMs, "custom-setup-idle") || !command(pair[0], pair + 1, 1)) return false;
    }
    Serial.println("[trial] configure=sf32rc regs=30:04,41:00,E0:02,E6:19");
    return true;
  }
};

template <unsigned Count>
uint8_t nearest(PaletteEntry pixel, const PaletteEntry (&palette)[Count]) {
  uint32_t best = UINT32_MAX;
  uint8_t code = 0;
  for (const auto& entry : palette) {
    const int r = int(pixel.r) - entry.r, g = int(pixel.g) - entry.g, b = int(pixel.b) - entry.b;
    const uint32_t distance = r*r + g*g + b*b;
    if (distance < best) { best = distance; code = entry.code; }
  }
  return code;
}

// Shared source chart: six native anchors, RGB ramps, alternating mono bars.
PaletteEntry pixel(unsigned x, unsigned y) {
  if (x < 4 || x >= kWidth - 4 || y < 4 || y >= kHeight - 4) return kNative[0];
  if (y < 300) return kNative[(y / 100) * 2 + x / 200];
  if (y < 500) {
    const auto& ink = kNative[2 + (y - 300) / 50];
    const unsigned value = (x * 255) / (kWidth - 1);
    return {0, uint8_t((ink.r * value) / 255), uint8_t((ink.g * value) / 255), uint8_t((ink.b * value) / 255)};
  }
  return kNative[((x / 20) + (y / 20)) & 1];
}

enum class Frame { Clear, Native, ColorHigh, ColorLow, Mono };

bool frame(Trial& trial, Frame kind, bool quality = false) {
  const uint32_t began = millis();
  if (!trial.level(HIGH, kBusyReleaseMs, "frame-idle")) return false;
  trial.bus->beginTransaction();
  digitalWrite(trial.cs, LOW);
  bool ok = trial.bus->writeCommand(0x10, 8);
  trial.bus->wait();  // writeCommand starts asynchronously; sample BUSY after10 is sent.
  if (ok) ok = trial.level(HIGH, kBusyReleaseMs, "frame-ready");
  uint8_t row[kWidth / 2];
  for (unsigned y = 0; ok && y < kHeight; ++y) {
    if (!trial.budget("frame-transfer")) { ok = false; break; }
    for (unsigned x = 0; x < kWidth; x += 2) {
      uint8_t pair = 0;
      for (unsigned offset = 0; offset < 2; ++offset) {
        const auto p = pixel(x + offset, y);
        uint8_t value = 0;
        switch (kind) {
          case Frame::Clear: break;
          case Frame::Native: value = nearest(p, kNative); break;
          // clear_initial establishes the same all-zero old plane as sf32rc.
          case Frame::Mono: value = ((x + offset) / 40 + y / 40) & 1; break;
          case Frame::ColorHigh:
          case Frame::ColorLow: {
            // sf32rc encoder pins exact black to legal selector3.
            const uint8_t code = (p.r == 0 && p.g == 0 && p.b == 0) ? 3
                : quality ? nearest(p, quality_palette) : nearest(p, fast_palette);
            value = kind == Frame::ColorHigh ? code >> 3 : code & 7;
            break;
          }
        }
        pair |= value << (offset ? 0 : 4);
      }
      row[x / 2] = pair;
    }
    trial.bus->writeBytes(row, sizeof(row), true, false);
    trial.bus->wait();
    if ((y & 7) == 7) delay(1);
  }
  trial.bus->wait();
  digitalWrite(trial.cs, HIGH);
  trial.bus->endTransaction();
  if (!ok) Serial.println("[trial] FAILED frame command/ready");
  if (ok) ok = trial.level(HIGH, kBusyReleaseMs, "frame-finished");
  Serial.printf("[trial] frame=%d bytes=%u frame_ms=%lu result=%d\n",
                int(kind), kWidth*kHeight/2, (unsigned long)(millis() - began), ok ? 0 : -1);
  return ok;
}

bool cold_prepare(Trial& trial) {
  // Exact sf32rc cold preparation, keeping power through all following stages.
  return trial.lut(tickey_waveform_clear_initial) && frame(trial, Frame::Clear)
      && trial.board_power_on("clear-on") && trial.cycle(0x12, "clear-initial");
}

bool mono_run(Trial& trial, bool clean) {
  return trial.lut(clean ? tickey_waveform_mono_clear : tickey_waveform_mono)
      && frame(trial, Frame::Mono)
      && trial.cycle(0x12, clean ? "mono-clear" : "mono") && trial.off("mono-off");
}

bool interrupted_run(Trial& trial) {
  // Exact FreeInklight path uses native0/1 pixels, CDI0F and no customLUT.
  // Full addressed frame; no partial-window assumptions or prior-image history.
  const uint8_t cdi = 0x0f;
  return trial.command(0x50, &cdi, 1) && frame(trial, Frame::Mono)
      && trial.board_power_on("interrupted-on") && trial.command(0x50, &cdi, 1) && trial.interrupt_native();
}

bool run(Trial& trial, char mode) {
  if (mode == 'N') {
    return frame(trial, Frame::Native) && trial.board_power_on("native-on")
        && trial.cycle(0x12, "native-refresh") && trial.off("native-off") && trial.pause(180);
  }
  if (mode == 'I') return interrupted_run(trial);
  if (!cold_prepare(trial)) return false;
  if (mode == 'M' || mode == 'C') return mono_run(trial, mode == 'C');
  const bool quality = mode == 'Q';
  if (!trial.lut(quality ? tickey_waveform_color_quality_phase1 : tickey_waveform_color_fast_phase1)
      || !frame(trial, Frame::ColorHigh, quality) || !trial.cycle(0x12, "color-phase1")) return false;
  if (quality && (!trial.lut(tickey_waveform_color_quality_phase2)
      || !trial.cycle(0x12, "color-phase2"))) return false;
  if (!trial.lut(quality ? tickey_waveform_color_quality_phase3 : tickey_waveform_color_fast_phase2)
      || !frame(trial, Frame::ColorLow, quality) || !trial.cycle(0x12, "color-final")
      || !trial.off("color-off")) return false;
  return quality || frame(trial, Frame::ColorHigh);  // Original fast post-seed, no refresh.
}

lgfx::Panel_Device* checked_panel() {
  auto* panel = M5.Display.getPanel();
  auto* bus = panel ? panel->getBus() : nullptr;
  if (!bus || panel->config().panel_width != kWidth || panel->config().panel_height != kHeight
      || panel->config().pin_cs != 44 || panel->config().pin_busy != 11) {
    Serial.println("[trial] FAILED unexpected PaperColor panel/bus/pins");
    return nullptr;
  }
  return panel;
}

bool finish(Trial& trial, bool ok, const char* name, uint32_t began) {
  // Always discard custom registers/history. Reset/native init does not repaint.
  // Overall bound: <=100s transaction plus <=35s best-effort native recovery.
  trial.deadline = millis() + 35000;
  const bool restored = trial.native_init();
  const bool powered_off = restored && trial.off("restore-off");
  ok = ok && restored && powered_off;
  Serial.printf("[trial] %s mode=%s total_ms=%lu native_restored=%d off=%d\n",
                ok ? "DONE" : "FAILED", name, (unsigned long)(millis() - began), restored, powered_off);
  return ok;
}

}  // namespace

bool waveform_trial(char mode) {
  if (mode != 'F' && mode != 'Q' && mode != 'M' && mode != 'C' && mode != 'N' && mode != 'I') {
    Serial.printf("[trial] FAILED unsupported mode=%c\n", mode);
    return false;
  }
  auto* panel = checked_panel();
  if (!panel) return false;
  const uint32_t began = millis();
  Trial trial{panel->getBus(), panel->config().pin_cs, panel->config().pin_busy, began + 100000};
  Serial.printf("[trial] START mode=%c width=%u height=%u LUT_bytes=%u\n", mode, kWidth, kHeight, TICKEY_WAVEFORM_BYTES);
  const bool ok = trial.native_init() && (mode == 'N' || mode == 'I' || trial.configure_custom()) && run(trial, mode);
  const char name[] = {mode, 0};
  return finish(trial, ok, name, began);
}

}  // namespace rcpaper::dev
