# Habit Tracker (C + GTK)

A desktop habit tracker app built in C with GTK.

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
