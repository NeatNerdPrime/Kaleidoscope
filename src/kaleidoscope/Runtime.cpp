/* Kaleidoscope - Firmware for computer input devices
 * Copyright (C) 2013-2021  Keyboard.io, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "kaleidoscope/Runtime.h"
#include "kaleidoscope/LiveKeys.h"
#include "kaleidoscope/layers.h"
#include "kaleidoscope/keyswitch_state.h"

namespace kaleidoscope {

uint32_t Runtime_::millis_at_cycle_start_;
KeyAddr Runtime_::last_addr_toggled_on_ = KeyAddr::none();

Runtime_::Runtime_(void) {
}

// ----------------------------------------------------------------------------
void
Runtime_::setup(void) {
  // We are explicitly initializing the Serial port as early as possible to
  // (temporarily, hopefully) work around an issue on OSX. If we initialize
  // Serial too late, no matter what we do, we'll end up reading garbage from
  // the serial port. For more information, see the following issue:
  //   https://github.com/keyboardio/Kaleidoscope-Bundle-Keyboardio/pull/7
  //
  // TODO(anyone): Figure out a way we can get rid of this, and fix the bug
  // properly.
  device().serialPort().begin(9600);

  kaleidoscope::sketch_exploration::pluginsExploreSketch();
  kaleidoscope::Hooks::onSetup();

  device().setup();

  // Clear the keyboard state array (all keys idle at start)
  live_keys.clear();

  Layer.setup();
}

// ----------------------------------------------------------------------------
void
Runtime_::loop(void) {
  millis_at_cycle_start_ = millis();

  kaleidoscope::Hooks::beforeEachCycle();

#ifndef NDEPRECATED
  // For backwards compatibility. Some plugins rely on the handler for
  // `beforeReportingState()` being called every cycle. In most cases, they can
  // simply switch to using `afterEachCycle()`, but we don't want to simply
  // break those plugins.
  kaleidoscope::Hooks::beforeReportingState();
  // Also for backwards compatibility. If user code calls any code that directly
  // changes the HID report(s) at any point between an event being detected and
  // the end of `handleKeyEvent()` (most likely from `beforeReportingState()`),
  // we need to make sure that report doesn't just get discarded.
  hid().keyboard().sendReport();
#endif

  // Next, we scan the keyswitches. Any toggle-on or toggle-off events will
  // trigger a call to `handleKeyswitchEvent()`, which in turn will
  // (conditionally) result in a HID report. Note that each event gets handled
  // (and any resulting HID report(s) sent) as soon as it is detected. It is
  // possible for more than one event to be handled like this in any given
  // cycle, resulting in multiple HID reports, but guaranteeing that only one
  // event is being handled at a time.
  device().scanMatrix();

  kaleidoscope::Hooks::afterEachCycle();
}

// ----------------------------------------------------------------------------
void
Runtime_::handleKeyswitchEvent(KeyEvent event) {

  // This function strictly handles physical key events. Any event without a
  // valid `KeyAddr` gets ignored.
  if (!event.addr.isValid())
    return;

  // Ignore any (non-)event that's not a state change. This check should be
  // unnecessary, as we shouldn't call this function otherwise.
  if (!(keyToggledOn(event.state) || keyToggledOff(event.state)))
    return;

  // Set the `Key` value for this event.
  if (keyToggledOff(event.state)) {
    // When a key toggles off, set the event's key value to whatever the key's
    // current value is in the live keys state array.
    event.key = live_keys[event.addr];
    // If that key was masked, unmask it and return.
    if (event.key == Key_Masked) {
      live_keys.clear(event.addr);
      return;
    }
  } else if (event.key == Key_NoKey) {
    // When a key toggles on, unless the event already has a key value (i.e. we
    // were called by a plugin rather than `actOnMatrixScan()`), we look up the
    // value from the current keymap (overridden by `live_keys`).
    event.key = lookupKey(event.addr);
  }

  // Run the plugin event handlers
  auto result = Hooks::onKeyswitchEvent(event);

  // If an event handler changed `event.key` to `Key_Masked` in order to mask
  // that keyswitch, we need to propagate that, but since `handleKeyEvent()`
  // will recognize that value as the signal to do a fresh lookup, so we need to
  // set that value in `live_keys` now. The alternative would be changing it to
  // some other sentinel value, and have `handleKeyEvent()` change it back to
  // `Key_Masked`, but I think this makes more sense.
  //
  // Note: It is still important to let events with `Key_Masked` fall through to
  // `handleKeyEvent()`, because some plugins might still care about the event
  // regardless of its `Key` value, and more importantly, that's where we clear
  // masked keys that have toggled off. Alternatively, we could call
  // `live_keys.clear(addr)` for toggle-off events here, and `mask(addr)` for
  // toggle-on events, then return, short-cutting the call to
  // `handleKeyEvent()`. It should work, but some plugins might be able to use
  // that information.
  if (event.key == Key_Masked)
    live_keys.mask(event.addr);

  // Now we check the result from the plugin event handlers, and stop processing
  // if it was anything other than `OK`.
  if (result != EventHandlerResult::OK)
    return;

  // If all the plugin handlers returned OK, we proceed to the next step in
  // processing the event.
  handleKeyEvent(event);
}

// ----------------------------------------------------------------------------
void
Runtime_::handleKeyEvent(KeyEvent event) {

  // For events that didn't begin with `handleKeyswitchEvent()`, we need to look
  // up the `Key` value from the keymap (maybe overridden by `live_keys`).
  if (event.addr.isValid()) {
    if (keyToggledOff(event.state) || event.key == Key_NoKey) {
      event.key = lookupKey(event.addr);
    }
  }

  // If any `onKeyEvent()` handler returns `ABORT`, we return before updating
  // the Live Keys state array; as if the event didn't happen.
  auto result = Hooks::onKeyEvent(event);
  if (result == EventHandlerResult::ABORT)
    return;

  // Update the live keys array based on the new event.
  if (event.addr.isValid()) {
    if (keyToggledOff(event.state)) {
      live_keys.clear(event.addr);
    } else {
      live_keys.activate(event.addr, event.key);
    }
  }

  // If any `onKeyEvent()` handler returned a value other than `OK`, stop
  // processing now. Likewise if the event's `Key` value is a no-op.
  if (result != EventHandlerResult::OK ||
      event.key == Key_Masked ||
      event.key == Key_NoKey ||
      event.key == Key_Transparent)
    return;

  // If it's a built-in Layer key, we handle it here, and skip sending report(s)
  if (event.key.isLayerKey()) {
    Layer.handleLayerKeyEvent(event);
    return;
  }

  // The System Control HID report contains only one keycode, and gets sent
  // immediately on `pressSystemControl()` or `releaseSystemControl()`. This is
  // significantly different from the way the other HID reports work, where held
  // keys remain in effect for subsequent reports.
  if (event.key.isSystemControlKey()) {
    if (keyToggledOn(event.state)) {
      hid().keyboard().pressSystemControl(event.key);
    } else { /* if (keyToggledOff(key_state)) */
      hid().keyboard().releaseSystemControl(event.key);
    }
    return;
  }

  // Until this point, the old report was still valid. Now we construct the new
  // one, based on the contents of the `live_keys` state array.
  prepareKeyboardReport(event);

#ifndef NDEPRECATED
  // Deprecated handlers might depend on values in the report, so we wait until
  // the new report is otherwise complete before calling them.
  auto old_result = Hooks::onKeyswitchEvent(event.key, event.addr, event.state);
  if (old_result == EventHandlerResult::ABORT)
    return;

  if (old_result != EventHandlerResult::OK ||
      event.key == Key_Masked ||
      event.key == Key_NoKey ||
      event.key == Key_Transparent)
    return;
#endif

  // Finally, send the new keyboard report
  sendKeyboardReport(event);

  // Now that the report has been sent, let plugins act on it after the fact.
  // This is useful for plugins that need to react to an event, but must wait
  // until after that event is processed to do so.
  Hooks::afterReportingState(event);
}

// ----------------------------------------------------------------------------
void
Runtime_::prepareKeyboardReport(const KeyEvent &event) {
  // before building the new report, start clean
  device().hid().keyboard().releaseAllKeys();

  // Build report from composite keymap cache. This can be much more efficient
  // with a bitfield. What we should be doing here is going through the array
  // and checking for HID values (Keyboard, Consumer, System) and directly
  // adding them to their respective reports. This comes before the old plugin
  // hooks are called for the new event so that the report will be full complete
  // except for that new event.
  for (KeyAddr key_addr : KeyAddr::all()) {
    // Skip this event's key addr; we will deal with that later. This is most
    // important in the case of a key release, because we can't safely remove
    // any keycode(s) added to the report later.
    if (key_addr == event.addr)
      continue;

    Key key = live_keys[key_addr];

    // If the key is idle or masked, we can ignore it.
    if (key == Key_Inactive || key == Key_Masked)
      continue;

#ifndef NDEPRECATED
    // Only run hooks for plugin keys. If a plugin needs to do something every
    // cycle, it can use one of the every-cycle hooks and search for active keys
    // of interest.
    auto result = Hooks::onKeyswitchEvent(key, key_addr, IS_PRESSED | WAS_PRESSED);
    if (result == EventHandlerResult::ABORT)
      continue;

    if (key_addr == event.addr) {
      // update active keys cache?
      if (keyToggledOn(event.state)) {
        live_keys.activate(event.addr, key);
      } else {
        live_keys.clear(event.addr);
      }
    }
#endif

    addToReport(key);
  }
}

// ----------------------------------------------------------------------------
void
Runtime_::addToReport(Key key) {
  // First, call any relevant plugin handlers, to give them a chance to add
  // other values to the HID report directly and/or to abort the automatic
  // adding of keycodes below.
  auto result = Hooks::onAddToReport(key);
  if (result == EventHandlerResult::ABORT)
    return;

  if (key.isKeyboardKey()) {
    // The only incidental Keyboard modifiers that are allowed are the ones on
    // the key that generated the event, so we strip any others before adding
    // them. This might turn out to be too simple to cover all the corner cases,
    // but the OS should be helpful and do most of the masking we want for us.
    if (!key.isKeyboardModifier())
      key.setFlags(0);

    hid().keyboard().pressKey(key);
    return;
  }

  if (key.isConsumerControlKey()) {
    hid().keyboard().pressConsumerControl(key);
    return;
  }

  // System Control keys and Layer keys are not handled here because they only
  // take effect on toggle-on or toggle-off events, they don't get added to HID
  // reports when held.
}

// ----------------------------------------------------------------------------
void
Runtime_::sendKeyboardReport(const KeyEvent &event) {
  // If the keycode for this key is already in the report, we need to send an
  // extra report without that keycode in order to correctly process the
  // rollover. It might be better to exempt modifiers from this rule, but it's
  // not clear that would be better.
  if (keyToggledOn(event.state) &&
      event.key.isKeyboardKey()) {
    // last keyboard key toggled on
    last_addr_toggled_on_ = event.addr;
    if (hid().keyboard().isKeyPressed(event.key)) {
      // The keycode (flags ignored) for `event.key` is active in the current
      // report. Should this be `wasKeyPressed()` instead? I don't think so,
      // because (if I'm right) the new event hasn't been added yet.
      hid().keyboard().releaseKey(event.key);
      hid().keyboard().sendReport();
    }
    if (event.key.getFlags() != 0) {
      hid().keyboard().pressModifiers(event.key);
      hid().keyboard().sendReport();
    }
  } else if (event.addr != last_addr_toggled_on_) {
    // (not a keyboard key OR toggled off) AND not last keyboard key toggled on
    Key last_key = live_keys[last_addr_toggled_on_];
    if (last_key.isKeyboardKey()) {
      hid().keyboard().pressModifiers(last_key);
    }
  }
  if (keyToggledOn(event.state)) {
    addToReport(event.key);
  }

#ifndef NDEPRECATED
  // Call old pre-report handlers:
  Hooks::beforeReportingState();
#endif

  // Call new pre-report handlers:
  if (Hooks::beforeReportingState(event) == EventHandlerResult::ABORT)
    return;

  // Finally, send the report:
  device().hid().keyboard().sendReport();
}

Runtime_ Runtime;

} // namespace kaleidoscope

kaleidoscope::Runtime_ &Kaleidoscope = kaleidoscope::Runtime;
