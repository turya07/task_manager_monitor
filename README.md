# task_manager_monitor (Linux only)

`task_manager_monitor` is a lightweight SFML desktop monitor that reads Linux system data from `/proc` and `/sys` and displays it in a simple GUI.

## Features

- CPU tab:
  - Live total CPU usage percentage.
  - Per-process CPU usage list (PID + process name), sorted by highest usage.
  - Small CPU usage trend graph.
  - CPU temperature readout.
- RAM tab:
  - Live RAM usage percentage.
  - RAM usage bar.

## Platform support

- Linux only (depends on Linux-specific paths such as `/proc/*` and `/sys/class/hwmon/*`).

## Dependencies

- CMake (project uses `CMakeLists.txt`)
- C++ compiler with modern C++ support
- SFML 2.6 (`window`, `graphics`, `system`)

## Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

Optional helper script:

```bash
./setup.sh
```

This builds the `task_monitor` executable (and `setup.sh` copies it to the repository root).

## Run

If built in the `build` directory:

```bash
./build/task_monitor
```

Or if copied by `setup.sh`:

```bash
./task_monitor
```

## Controls

- Click top tabs (`CPU` / `RAM`) to switch views.
- `Tab` key switches between tabs.
- On CPU tab:
  - Click next/previous buttons to change pages.
  - `Shift + Right` / `Shift + Left` for next/previous page.
- `Esc` or window close button exits the app.

## Notes / current limitations

- The app loads font from:
  - `/home/tb07/.local/share/fonts/Poppins-Regular.ttf`
  - Update this path in `main.cpp` if this font does not exist on your machine.
- CPU temperature is read from:
  - `/sys/class/hwmon/hwmon4/temp1_input`
  - This path can vary by system; adjust in `main.cpp` if needed.
- Requires access to Linux procfs/sysfs data sources.

