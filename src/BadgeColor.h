#pragma once

#include <d2d1.h>

#include <string>

// A stable colour for a row's badge, derived from a name: the same name is always the same colour
// and two different ones are told apart at a glance. Every colour has the same lightness and
// saturation, which is what makes an arbitrary name still look deliberate, and the lightness
// follows the panel's theme, because a badge has to separate from the surface it sits on and one
// value cannot do that for both a light and a dark panel. DrawBadge picks the letter colour from
// the result, so any lightness stays readable.
D2D1_COLOR_F HashedBadgeColor(const std::wstring& key);
