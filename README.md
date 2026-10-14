# tmux hints for KRunner

A KRunner plugin that works like an interactive tmux cheatsheet. Open KRunner
(Alt+Space), start typing a tmux command, and it shows what you can type next
with a short explanation of each option. It also lists your running tmux
sessions so you can jump straight into one.

Tired of looking up whether it's `split-window -h` or `-v`, or what `attach -d`
does? Just type it and read the hint.

## What it does

<img width="790" height="480" alt="tmux_hints_gif_example" src="https://github.com/user-attachments/assets/0a784869-685b-48d6-8fda-e0db4283e0bc" />



| You type | You see |
|---|---|
| `tmux` | Your running sessions (Enter attaches), then every tmux command with a description |
| `tmux att` | Matching commands, e.g. `attach-session (attach, a)` |
| `tmux attach` | The flags for `attach`: `-d` detach other clients, `-r` read-only, `-t <target-session>` … |
| `tmux attach -t ` | The names of your actual running sessions |
| `tmux splitw -t ` | Your live panes (`work:1.0`) and what's running in them |
| `tmux select-layout ` | The layout names: `even-horizontal`, `tiled`, … |

- **Enter on a suggestion** puts it into KRunner and keeps the window open, so
  you can build a command step by step.
- **Enter on "Run: tmux …"** runs the command in your default terminal.
  Interactive commands (`attach`, `new` without `-d`) take over the terminal;
  others run and wait for Enter so you can read the output.
- **Enter on a session** attaches to it. Extra actions let you attach while
  detaching other clients, or copy the command to the clipboard.

It understands tmux aliases (`ls`, `neww`, `splitw`), unique prefixes (`a`),
and combined flags (`-dr`).

## Requirements

- KDE Plasma 6 (KDE Frameworks 6, Qt 6)
- tmux
- A C++20 compiler and CMake

## Install from source

### 1. Install the build dependencies

**Arch / Manjaro / EndeavourOS**
```sh
sudo pacman -S --needed base-devel cmake extra-cmake-modules krunner kio qt6-base tmux
```

**Fedora**
```sh
sudo dnf install gcc-c++ cmake extra-cmake-modules kf6-krunner-devel kf6-kio-devel qt6-qtbase-devel tmux
```

**Debian / Ubuntu / KDE neon** (a release that ships Plasma 6)
```sh
sudo apt install build-essential cmake extra-cmake-modules libkf6runner-dev libkf6kio-dev qt6-base-dev tmux
```

**openSUSE Tumbleweed**
```sh
sudo zypper install gcc-c++ cmake extra-cmake-modules kf6-krunner-devel kf6-kio-devel qt6-base-devel tmux
```

### 2. Build and install

```sh
git clone https://github.com/ChristianHrs/tmux_hints.git
cd tmux_hints
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

### 3. Restart KRunner

```sh
kquitapp6 krunner
```

KRunner starts again the next time you press Alt+Space. Type `tmux` to try it.

If nothing shows up, check that **Tmux Cheatsheet** is enabled in
*System Settings → Search → Plasma Search*.

## Uninstall

CMake records every installed file, so you can remove them with:

```sh
sudo xargs rm < build/install_manifest.txt
kquitapp6 krunner
```

## Adding or changing commands

All commands, flags and descriptions live in [`tmuxcommands.h`](tmuxcommands.h)
as a plain table. Add an entry, rebuild, reinstall and restart KRunner. The
runner logic doesn't need to change.

## Troubleshooting

- **CMake can't find `ECM`**: install `extra-cmake-modules` (see step 1).
- **CMake can't find `KF6Runner` or `KF6KIO`**: install the KRunner/KIO
  development packages for your distro (see step 1).
- **Plugin doesn't appear**: run `kquitapp6 krunner; krunner` in a terminal
  and look for errors mentioning `krunner_tmux`.
- **No sessions are listed**: the plugin runs `tmux list-sessions`; make sure
  a tmux server is running (`tmux ls` in a terminal).

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
