# ClipForge

A lightweight clipboard manager for Linux — daemon-based, terminal-native, built in pure C++.

ClipForge runs silently in the background and remembers everything you copy. Search your history, save named snippets, and never lose a copied item again. Sensitive data like API keys and tokens are automatically deleted after 30 seconds.

---

## The Problem

The OS clipboard holds exactly one item. Every time you copy something new, the previous item is gone forever. As a developer you copy file paths, commands, URLs, tokens, and code snippets constantly — and spend time re-finding things you already copied.

ClipForge solves this by persisting your entire clipboard history locally, with a fast CLI to retrieve anything instantly.

---

## Features

- Persistent clipboard history across sessions
- Full-text search through history
- Named snippets for frequently used content
- Automatic content type detection — URL, path, code, secret, text
- Sensitive data guard — secrets auto-delete after 30 seconds
- Pin important items so they are never evicted
- Runs as a systemd user service — starts automatically on login
- Zero network access — all data stays on your machine
- Interactive ncurses TUI — `clip ui` for visual browsing

---
```
## Architecture

Wayland/X11 clipboard
│
▼
┌───────────────────┐
│   ClipForge       │  Background daemon
│   Daemon          │  ├── Clipboard watcher thread
│                   │  ├── IPC server thread (Unix socket)
│                   │  └── Secret cleanup timer thread
└────────┬──────────┘
│ Unix domain socket
│ /tmp/clipforge.sock
▼
┌───────────────────┐
│   ClipForge CLI   │  clip list / get / search / ...
└───────────────────┘
│
▼
┌───────────────────┐
│   SQLite DB       │  ~/.local/share/clipforge/history.db
└───────────────────┘
```

The daemon and CLI are separate processes that communicate over a Unix domain socket using fixed-size binary message structs. The daemon captures clipboard changes by polling `xclip` every 2 seconds, stores items in SQLite with content type detection, and serves CLI requests over IPC.

---

## Tech Stack

| Component | Technology |
|-----------|------------|
| Language | C++17 |
| Build system | CMake |
| Database | SQLite3 (C API — no ORM) |
| IPC | Unix domain sockets (POSIX) |
| Daemon | fork/setsid/fork pattern |
| Signals | SIGTERM/SIGINT handlers |
| Threading | std::thread |
| Auto-start | systemd user service |
| Clipboard | xclip / wl-paste |

---

## Requirements

- Linux (Wayland or X11)
- g++ with C++17 support
- CMake 3.16+
- SQLite3 (`libsqlite3-dev`)
- ncurses (`libncurses-dev`)
- xclip (`sudo apt install xclip`)

---

## Install

```bash
git clone https://github.com/Negi2110/clipforge.git
cd clipforge
sudo apt install libsqlite3-dev libncurses-dev xclip cmake
./install.sh
```

Add to your `~/.bashrc` if not already present:
```bash
export PATH="$HOME/.local/bin:$PATH"
```

---

## Usage

```bash
# history
clip list                  # show last 20 items
clip list 50               # show last 50 items
clip get <id>              # copy item back to clipboard
clip search <query>        # search history

# management
clip delete <id>           # delete one item
clip clear                 # clear all history (keeps pinned)
clip pin <id>              # pin item — never evicted
clip unpin <id>            # unpin item

# snippets
clip save <name>           # save current clipboard as named snippet
clip get <name>            # copy snippet to clipboard
clip snippets              # list all snippets

# info
clip stats                 # show usage statistics
clip help                  # show all commands

# daemon
clip daemon start          # start background daemon
clip daemon stop           # stop daemon
clip daemon status         # check if running

# TUI
clip ui                    # launch interactive browser
                           # j/k: navigate, Enter: copy
                           # d: delete, p: pin, /: search
```

---

## How It Works

**Daemon lifecycle** — on `clip daemon start` the process forks twice (classic Unix daemonize pattern), creates a new session with `setsid()`, redirects stdio to `/dev/null`, and writes its PID to a file. The grandchild process is the actual daemon — it cannot reacquire a terminal under any circumstance.

**Clipboard capture** — a background thread polls `xclip` every 2 seconds. On change, content is classified by type (URL, file path, code, secret, plain text) using regex matching, then inserted into SQLite. Consecutive duplicates are rejected at the storage layer.

**IPC** — the daemon listens on a Unix domain socket. Each CLI invocation connects, sends a fixed-size `Message` struct containing a command and payload, reads back a fixed-size `Response` struct, and disconnects. Fixed-size structs eliminate the framing problem — both sides always know exactly how many bytes to read.

**Sensitive data** — content matching hex or base64 patterns is classified as `secret` and saved with `expires_at = now + 30s`. A timer thread runs every 5 seconds and deletes expired rows with a single SQL `DELETE WHERE expires_at < now`.

**Storage** — SQLite with two tables: `history` for clipboard items and `snippets` for named permanent entries. History items use `AUTOINCREMENT` IDs that never reuse values. LRU eviction deletes oldest unpinned rows when count exceeds `max_history`.

---

## Configuration

Edit `~/.config/clipforge/config.ini`:

```ini
# ClipForge configuration
db_path=/home/user/.local/share/clipforge/history.db
socket_path=/tmp/clipforge.sock
pid_path=/home/user/.local/share/clipforge/clipforge.pid
log_path=/home/user/.local/share/clipforge/clipforge.log
max_history=1000
sensitive_timeout_seconds=30
```

Restart the daemon after editing: `clip daemon stop && clip daemon start`

---

## Known Limitations

- Clipboard polling causes minor visual jitter on GNOME Wayland panels. This is a compositor limitation — `wl-paste --watch` requires a wlroots compositor (Sway, Hyprland). Native GNOME clipboard API support is planned.
- Binary clipboard content (images, files) is not captured — text only.

---

## Uninstall

```bash
./uninstall.sh
```

---

## License

MIT
ENDOFREADME