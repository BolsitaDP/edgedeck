# Changelog

What changes between releases, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

The first public version. EdgeDeck so far:

### Tabs
- Quick Actions: your own list of programs, documents, folders and web addresses, each with its
  icon, edited in its own window or by dragging files and shortcuts onto it.
- Recent files: the newest files in a folder (Downloads by default).
- Media, Volume, Brightness (external monitors over DDC/CI, and a laptop's built-in screen) and
  Lyrics (LRCLIB).
- Volume switches the default output in one click and mutes the microphone.
- Lyrics follow the song line by line where LRCLIB has them synced, and otherwise show the plain
  lyrics, scrolling with the song; of several versions, the one as long as the track playing.
  Synced lyrics can be moved half a second at a time when they are timed to another release of the
  song (remembered for that song), and a click on a line plays the song from it.
- Media names each app as you know it, including those that only tell Windows they embed Chromium
  (Spotify).
- Displays: switch which monitors are on, between layouts you save (up to nine, each with a
  shortcut) or, with none saved, all monitors and each one alone. An existing setup's four fixed
  layouts are carried over as saved ones.
- Any number of tabs, on the left or right edge of any monitor, with their own size and position.

### Behaviour
- Panels open on hover, never take the focus, and can be pinned open.
- A tab is moved up or down its edge by dragging the handle in its panel's corner, and stays there.
- The tabs step aside over a full-screen app on their monitor, as the taskbar does, and no panel
  opens over one; an optional open delay; Pause tabs.
- A first start shows three tabs that work on any machine (Quick Actions, Media, Volume) and a
  notification saying where they are and where Settings is.
- A notification-area icon with Settings, quick actions, pause, Start with Windows, the log and
  restart.
- English and Spanish, chosen in Settings; a first start speaks Windows' language if it is one of
  them.
- Configurable system-wide shortcuts (exit, pause, open tab 1-9, switch displays 1-9), each of which
  can be turned off; a shortcut another program owns is reported instead of silently missing.
- Light, dark and high-contrast themes that follow Windows and its accent colour.
- A crash leaves a log line and a minidump and restarts EdgeDeck once.
- Starting EdgeDeck again while it runs opens its Settings, instead of seeming to do nothing.
- *Report a problem...* opens a bug report already filled in, with the end of the log on the
  clipboard.
- On a screen too short for it (1366x768 at 125%, 1080p at 175%), Settings fits the work area and
  scrolls, so Save is always reachable.
- A settings file that cannot be read, or that a newer version wrote, is copied to
  `config.backup.txt` before it is replaced, and EdgeDeck says so. A file saved as ANSI by an editor
  keeps its accents.

### Distribution
- A setup program (Inno Setup) in English and Spanish that needs no administrator rights, updates
  in place and closes a running EdgeDeck cleanly; and a zip with Install.cmd for those who prefer
  it. Both carry the C++ runtime built in; x64 and ARM64.
- Listed in Windows' installed apps (per user), with its version and an Uninstall button.
- Opened straight from inside the zip, EdgeDeck and Install.cmd say to unzip it first, and Start
  with Windows refuses to point at a temporary folder that Windows will clean away.
- *Check for updates* in the menu opens the latest release; nothing is checked in the background.
- Each release carries its winget manifests, for installing with `winget` once they are accepted.
- MIT license.
