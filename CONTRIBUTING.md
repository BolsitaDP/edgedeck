# Contributing to EdgeDeck

Thanks for helping. Bug reports, translations, documentation and code are all welcome.

## Reporting a bug

The quickest way is EdgeDeck's menu > *Report a problem...*: it opens the bug template with the
version, Windows and monitors filled in, and puts the end of the log on the clipboard. Otherwise,
open an issue with the bug template. What helps most:

- your Windows version, display scaling and monitor setup (how many, which is primary);
- what you did, what happened, and what you expected;
- the end of `%LOCALAPPDATA%\EdgeDeck\edgedeck.log` (menu > *Open log*);
- after a crash, the newest `.dmp` file from `%LOCALAPPDATA%\EdgeDeck\crashes\`.

For a security problem, see [SECURITY.md](SECURITY.md) instead of opening a public issue.

## The rules every change keeps

EdgeDeck exists to sit at the edge of the screen costing nothing and interrupting nobody. A change
that breaks one of these needs a very good reason:

1. **Nothing runs while nothing happens.** No polling, no render loop, no timers while the panels are
   closed, no global mouse or keyboard hooks. React to events.
2. **Never take the focus.** A panel must not activate, and nothing it does may put a window in front
   of the user. Problems are reported with `PanelWidget::ReportProblem`, not with a dialog.
3. **Measure what it costs.** Compare working set, private bytes, handles and threads with the
   previous build - idle *and* after the new thing has been used once
   (`tools/scripts/Measure-Perf.ps1`). Shell and COM objects are the usual surprise: prefer plain
   Win32 and file reads.
4. **Blocking work goes off the UI thread** and answers with one posted message (see *Asynchronous
   results* in [docs/DESIGN.md](docs/DESIGN.md)).
5. **No new dependencies.** Win32, Direct2D, DirectWrite and the Windows SDK; no third-party
   libraries, not even in the tests.

## Building

CMake 3.20+, Visual Studio 2022 (or Build Tools) with the C++ desktop workload and a Windows SDK.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DEDGEDECK_BUILD_TOOLS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`EDGEDECK_BUILD_TOOLS=ON` also builds the desktop test tools. To run your build as an installed copy
(so rebuilding does not fight a running `EdgeDeck.exe`): `scripts\install.ps1 -Build`.

`scripts\package.ps1` makes the release zip and `scripts\installer.ps1` the setup program, from
`installer\EdgeDeck.iss`. The setup needs Inno Setup 6: install it from its site, or let
`scripts\get-innosetup.ps1` fetch the version CI uses (pinned, checked by SHA256 and signature,
installed for your user only).

## Testing

- **Unit tests** (`tests/`, run by `ctest` and in CI) cover everything without a window. Add tests
  for new logic there; keep logic out of window code so it can be tested.
- **Desktop tests** (`tools/scripts/`) check what needs a screen: layout at every scale and in every
  language, pixels of the panels, clicks, drag and drop. **They move your real cursor, click, open
  windows and some switch monitors** - read [tools/README.md](tools/README.md) first, and run them
  when you can leave the machine alone for a few minutes.
- **See the test fail.** Before trusting a new test, break the code it guards and watch it fail.
- **Static analysis** (MSVC `/analyze`) runs in CI with every finding an error, and is at zero. To
  run it locally:

  ```powershell
  cmake -S . -B build-analyze -G "Visual Studio 17 2022" -A x64 "-DCMAKE_CXX_FLAGS=/analyze /analyze:external- /EHsc /WX"
  cmake --build build-analyze --config Release --target EdgeDeck
  ```

  A finding that is wrong is suppressed on its own line (`#pragma warning(suppress : NNNN)`) with
  a comment saying why.

## Code style

- C++23, MSVC, warnings at `/W4` must stay clean.
- Match the code around you: names, layout, and comments that explain *why* something is done the
  way it is, especially when the obvious way was tried and failed.
- Every user-facing text goes through the string table (`TR(...)` / `Strings::Format`), never a
  literal in the UI code.
- The log (`Diagnostics`) stays in English.

## Adding a widget

1. Add a value to `WidgetType` (`src/PanelWidget.h`) and give it a name in `config.txt`
   (`WidgetTypeToString` / `WidgetTypeFromString` in `src/Config.cpp`).
2. Implement `PanelWidget` in `src/<Name>Widget.h/.cpp`: title, tab icon, height, `Draw`, `HitTest`,
   `Activate`, and keyboard and accessibility text. Start any work in `OnPanelOpening`, never at
   start-up.
3. Create it in `MakeWidget` (`src/App.cpp`) and offer it in Settings (`kTypes` in
   `src/SettingsWindow.cpp`).
4. Add its texts to `src/Strings.h`, in every language.
5. Add the sources to `CMakeLists.txt`, unit tests for its logic, and a line in both READMEs.

## Adding or improving a translation

Texts live in `src/Strings.h`, one `X(Id, English, Spanish)` entry per text.

- **Improving a translation:** edit the text. Keep every `%ls`, `%zu`, `%d`... in the same order as
  the English - a unit test checks this, because the values are passed once for every language.
- **Adding a language:** add it to `Language`, raise `kLanguageCount`, add a column to every entry
  (and to the `X` macro's parameters in `Strings.h` and `Strings.cpp`), and give it a code and its own
  name in `Strings.cpp`. Then check the Settings and actions windows fit:
  `tools/scripts/Test-SettingsLayout.ps1 -Language <code>` (and `-Dialog actions`).

## Pull requests

- One topic per pull request; small is easier to review.
- Say what changed and why, how you tested it, and what it costs (rule 3) if it touches anything
  that runs.
- CI builds the app, the tests and the tools, and runs the unit tests; it must pass.

## Releasing

For maintainers. A release is a tag; everything else is the `release` workflow.

1. Rehearse (optional, publishes nothing): Actions > release > *Run workflow* with the version.
   It builds, tests and packages both architectures and keeps the zips, the notes it would publish
   (from `## [Unreleased]`) and the winget manifests as a build artifact.
2. In CHANGELOG.md, rename `## [Unreleased]` to `## [X.Y.Z] - YYYY-MM-DD` and commit. The
   workflow refuses a tag whose version has no section there.
3. Tag and push: `git tag vX.Y.Z` and `git push origin vX.Y.Z`. The workflow publishes the
   release with the x64 and ARM64 setup programs and zips, their symbols and SHA256s, and
   `EdgeDeck-X.Y.Z-winget-manifests.zip`.
4. winget: unzip the manifests into a fork of
   [microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs) (they are already in its
   `manifests/b/BolsitaDP/EdgeDeck/X.Y.Z` layout) and open a pull request. `scripts/winget.ps1`
   makes the same files locally and validates them with `winget validate`.

By contributing you agree that your contribution is licensed under the [MIT license](LICENSE).
