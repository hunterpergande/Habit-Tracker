# Habit Tracker (C + GTK)

A desktop habit tracker app built in C with GTK.

## Quick Start (Recommended)

Download a prebuilt binary from the latest GitHub Release:

- Linux: `habit-tracker-linux-x64.tar.gz`
- macOS: `habit-tracker-macos-x64.tar.gz`
- Windows: `habit-tracker-windows-x64.zip`

Release page:

```text
https://github.com/hunterpergande/Habit-Tracker/releases/latest
```

### Run downloaded builds

- Linux/macOS:

	- Ensure GTK 3 runtime is installed (`libgtk-3-0` on Ubuntu/Debian, `gtk+3` on macOS via Homebrew).

```bash
tar -xzf habit-tracker-*.tar.gz
cd habit-tracker-*
chmod +x habit-tracker
./habit-tracker
```

- Windows:
	- Extract `habit-tracker-windows-x64.zip`
	- Open the extracted folder
	- Double-click `habit-tracker.exe`

## Features

- Track up to 10 habits
- Choose 7, 30, 60, or 80 day cycles
- Mark daily completion with a checkbox grid
- Rename habits in-app
- Export progress statistics

## Requirements

- GCC
- GTK 3 development packages
- `pkg-config`

On Debian/Ubuntu-based systems:

```bash
sudo apt install build-essential libgtk-3-dev pkg-config
```

## Build

```bash
gcc App.c -o habit-tracker $(pkg-config --cflags --libs gtk+-3.0)
```

## Run

```bash
./habit-tracker
```

## Project Files

- `App.c` — main application source
- `settings.dat` / `states.dat` / `habits.dat` — local app data created at runtime
- `stats_export.txt` — optional export file created when stats are exported

## CI / Release Automation

- Every push/PR builds on Linux, macOS, and Windows using GitHub Actions.
- Publishing a GitHub Release automatically creates platform artifacts for download.
