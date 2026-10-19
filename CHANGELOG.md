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
- Media, Volume, Brightness (DDC/CI) and Lyrics (LRCLIB).
- Displays: switch which monitors are on, between layouts you save (up to nine, each with a
  shortcut) or, with none saved, all monitors and each one alone. An existing setup's four fixed
  layouts are carried over as saved ones.
- Any number of tabs, on the left or right edge of any monitor, with their own size and position.

### Behaviour
- Panels open on hover, never take the focus, and can be pinned open.
- No panel over a full-screen app; an optional open delay; Pause tabs.
- A first start shows three tabs that work on any machine (Quick Actions, Media, Volume) and a
  notification saying where they are and where Settings is.
- A notification-area icon with Settings, quick actions, pause, Start with Windows, the log and
  restart.
- English and Spanish, chosen in Settings.
- Configurable system-wide shortcuts (exit, pause, open tab 1-9, switch displays 1-4), each of which
  can be turned off; a shortcut another program owns is reported instead of silently missing.
- Light, dark and high-contrast themes that follow Windows and its accent colour.
- A crash leaves a log line and a minidump and restarts EdgeDeck once.

### Distribution
- A release zip with an installer that needs no administrator rights, an uninstaller, and the C++
  runtime built in; x64 and ARM64.
- MIT license.
