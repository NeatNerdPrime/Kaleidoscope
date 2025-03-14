/* Kaleidoscope-LED-Palette-Theme -- Palette-based LED theme foundation
 * Copyright 2022-2025 Keyboard.io, inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * Additional Permissions:
 * As an additional permission under Section 7 of the GNU General Public
 * License Version 3, you may link this software against a Vendor-provided
 * Hardware Specific Software Module under the terms of the MCU Vendor
 * Firmware Library Additional Permission Version 1.0.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <Arduino.h>  // for PROGMEM

#include "kaleidoscope/device/device.h"  // for cRGB
#include "kaleidoscope/plugin.h"         // for Plugin

namespace kaleidoscope {
namespace plugin {

namespace ledpalette {
extern bool palette_defined;
extern const cRGB palette[];
}  // namespace ledpalette

class DefaultPalette : public Plugin {
 public:
  static void setup();
};

}  // namespace plugin
}  // namespace kaleidoscope

// clang-format off

#define PALETTE(p0, p1, p2, p3, p4, p5, p6, p7,           \
                p8, p9, pa, pb, pc, pd, pe, ...)          \
  namespace kaleidoscope {                                \
  namespace plugin {                                      \
  namespace ledpalette {                                  \
    const cRGB palette[] PROGMEM = {                      \
      p0, p1, p2, p3, p4, p5, p6, p7,                     \
      p8, p9, pa, pb, pc, pd, pe, ##__VA_ARGS__           \
    };                                                    \
    bool palette_defined = true;                          \
  } /* defaultcolormap */                                 \
  } /* plugin */                                          \
  } /* kaleidoscope */

// clang-format on

extern kaleidoscope::plugin::DefaultPalette DefaultPalette;
