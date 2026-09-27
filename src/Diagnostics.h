#pragma once

#include <string>

// Minimal file-backed logging.
//
// EdgeDeck is a windowed app with no console, and most of its failure modes are
// silent by nature: a D2D surface that could not be created, a config file that
// could not be written, a tab that could not be built. Before this existed, all
// of those simply did nothing and were impossible to diagnose after the fact.
//
// Writes go to %LOCALAPPDATA%\EdgeDeck\edgedeck.log, appended, one line per
// entry, and are capped so a runaway loop cannot fill the disk. Logging is
// cheap enough to leave on: every entry is a single fopen/fwrite/fclose, and
// only failures and lifecycle events are ever recorded - not per-frame drawing.

namespace Diagnostics {

// Human-readable path of the log file, for the settings UI to show.
std::wstring LogPath();

// Records one line. `level` is written verbatim ("error", "info").
void Write(const char* level, const char* format, ...);

// Convenience wrappers, so call sites read as prose.
void Error(const char* format, ...);
void Info(const char* format, ...);

// Last Win32 error, formatted as "error 5 (Access is denied)".
std::string LastErrorText();

} // namespace Diagnostics
