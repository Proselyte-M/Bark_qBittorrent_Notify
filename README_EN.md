# qBittorrent Bark Notifier (Windows)

This is a lightweight C tool that sends Bark push notifications when qBittorrent starts or finishes a download. It runs as a Windows GUI subsystem app (no console window) and writes logs to a file in the same directory.

## Features

- Supports all 12 qBittorrent placeholders
- Structured message with human‑readable size
- INI configuration file (no dependency on environment variables)
- UTF‑8 and URL‑encoding support
- Detailed logging for troubleshooting

## Requirements

- Windows 10/11
- Clang/MinGW or MSVC

## Build

### Clang/MinGW

```powershell
clang bark_notify.c -o bark_notify.exe -lwinhttp -lshell32 -luser32 -lkernel32 -static -mwindows
```

### CMake

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Configuration

The program reads `bark_notify.ini` from the same directory as the exe. UTF‑8 BOM and spaces are supported.

```ini
[Bark]
Key=YOUR_BARK_KEY
Server=api.day.app
```

Self‑hosted example:

```ini
[Bark]
Key=123
Server=127.0.0.1:8080
```

## Logs

Logs are written to `bark_notify.log` in the same directory as the executable.

## qBittorrent Setup

In “Options → Downloads → Run external program”:

On completion:

```bash
D:\path\to\bark_notify.exe -E completed -N "%N" -L "%L" -G "%G" -F "%F" -R "%R" -D "%D" -C "%C" -Z "%Z" -T "%T" -I "%I" -J "%J" -K "%K"
```

On start:

```bash
D:\path\to\bark_notify.exe -E start -N "%N" -L "%L" -G "%G" -F "%F" -R "%R" -D "%D" -C "%C" -Z "%Z" -T "%T" -I "%I" -J "%J" -K "%K"
```

## Placeholder Mapping

| Flag | Meaning | Placeholder |
|---|---|---|
| -N | Torrent name | %N |
| -L | Category | %L |
| -G | Tags | %G |
| -F | Content path | %F |
| -R | Root path | %R |
| -D | Save path | %D |
| -C | File count | %C |
| -Z | Size in bytes | %Z |
| -T | Tracker | %T |
| -I | Hash v1 | %I |
| -J | Hash v2 | %J |
| -K | Torrent ID | %K |
| -E | Event | start / completed |

## Troubleshooting

- “Key not found” in log: check `bark_notify.ini` and make sure the key is set as `Key=...`
- HTTP 400: usually invalid key or request parameters
- Connection errors: verify server address and port

## License

MIT
