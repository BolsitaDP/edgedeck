# Security

## Reporting a vulnerability

Please do not open a public issue for a security problem. Report it privately through GitHub:
**Security > Report a vulnerability** on this repository
([direct link](https://github.com/BolsitaDP/edgedeck/security/advisories/new)).

Include what an attacker could do, the steps to reproduce it, and the EdgeDeck and Windows versions.
You will get an answer as soon as possible; once a fix is released the report is credited unless you
ask otherwise.

## What EdgeDeck touches

Useful to know when judging a report:

- It runs as the signed-in user, never elevated, and installs per user
  (`%LOCALAPPDATA%\EdgeDeck`, `HKCU\...\Run`).
- It reads `config.txt`, shortcut (`.lnk`) files dropped on it or listed by the Recent files tab, and
  icon resources from programs; all of these are parsed with bounds checks.
- It opens what the user configured (Quick Actions) or clicked (Recent files) with `ShellExecuteEx`.
- Its only network traffic is HTTPS to `lrclib.net`, and only when a Lyrics tab exists.

Only the latest release receives fixes.
