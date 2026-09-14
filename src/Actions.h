#pragma once

// Native Windows actions invoked from the EdgeDeck panel.
// Deliberately tiny: this MVP only needs to prove the panel can drive the OS.
namespace Actions {

void OpenNotepad();
void OpenCalculator();
void ShowDesktopToggle();

} // namespace Actions
