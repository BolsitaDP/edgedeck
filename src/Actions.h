#pragma once

// Native Windows actions invoked from the EdgeDeck panel.
// Deliberately tiny: this MVP only needs to prove the panel can drive the OS.
namespace Actions {

// Return false when Windows cannot perform the requested action.
bool OpenNotepad();
bool OpenCalculator();
bool ShowDesktopToggle();

} // namespace Actions
