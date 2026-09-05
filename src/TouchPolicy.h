#pragma once

#include <HalGPIO.h>

#include "CrossPointSettings.h"

// One place that answers "may this touch do anything, and are the hint boxes on
// screen". Before this existed, gpio.hasTouch() answered both questions at once:
// a board with a digitizer got no hint boxes and a fully live screen, and there
// was no way to ask for one without the other. Themes and MappedInputManager
// now ask here instead, so the three modes are decided once rather than in
// thirty call sites.
//
// hasTouch() is read live rather than cached: the touch controller finishes its
// init after static construction, so the flag flips once during boot.
namespace TouchPolicy {

inline bool panelPresent() { return gpio.hasTouch(); }

inline CrossPointSettings::TOUCH_MODE mode() {
  const uint8_t stored = SETTINGS.touchMode;
  if (stored >= CrossPointSettings::TOUCH_MODE_COUNT) return CrossPointSettings::TOUCH_ANYWHERE;
  return static_cast<CrossPointSettings::TOUCH_MODE>(stored);
}

// The whole screen is live: list rows, swipes, edge gestures, map panning.
inline bool touchAnywhere() { return panelPresent() && mode() == CrossPointSettings::TOUCH_ANYWHERE; }

// Only the hint boxes are live, and a tap on one acts as its hardware button.
inline bool touchHintBoxes() { return panelPresent() && mode() == CrossPointSettings::TOUCH_BUTTONS_ONLY; }

// Any touch at all reaches the UI. False in OFF, which is also why a stray
// touch must not count as user activity for the sleep timer.
inline bool touchActive() { return panelPresent() && mode() != CrossPointSettings::TOUCH_DISABLED; }

// Whether the six hint boxes are drawn, and whether the layout must reserve
// room for them. True on every board without a digitizer (an X4 labels its
// physical keys this way) and on a touch board in BUTTONS or OFF.
inline bool hintsVisible() { return !panelPresent() || mode() != CrossPointSettings::TOUCH_ANYWHERE; }

}  // namespace TouchPolicy
