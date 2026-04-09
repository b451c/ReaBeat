---
name: Bug Report
about: Something isn't working correctly
title: ''
labels: bug
assignees: ''
---

**What happened?**
A clear description of the bug.

**Steps to reproduce:**
1. Select item '...'
2. Click '...'
3. See error

**Expected behavior:**
What should have happened.

**Environment:**
- OS: [e.g. macOS 15.1, Windows 11, Ubuntu 24.04]
- REAPER version: [e.g. 7.24]
- ReaImGui version: [e.g. 0.9.3]
- REABeat version: [e.g. 1.0.0]

**Server log** (if applicable):
```
Paste contents of:
  macOS/Linux: /tmp/reabeat_server.log
  Windows: %TEMP%\reabeat_server.log
```

**Backend check:**
```
cd REABeat && uv run python -m reabeat check
```
Paste the output here.
