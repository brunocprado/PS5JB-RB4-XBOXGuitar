# PS5Instrumentalizer

Use **Xbox 360 Rock Band and Guitar Hero guitars** in **Rock Band 4** on a jailbroken **PS5**.

A PS5 port of [OrbisInstrumentalizer](https://github.com/InvoxiPlayGames/OrbisInstrumentalizer) (PS4 / GoldHEN) by InvoxiPlayGames.

> Tested on firmware 13.60 (Relapse) with a wired Xbox 360 guitar and Rock Band 4 `CUSA02084` v02.21.
> Wireless receivers are implemented but untested.

## Download

No need to build anything: grab the ready-to-use **`PS5Instrumentalizer.elf`** from this repository.

## Requirements

- A jailbroken PS5 (firmware 7.00 – 13.60, e.g. [Relapse](https://github.com/ntfargo/Relapse-Exploit)) with **etaHEN** loaded.
- The **PS4 version** of Rock Band 4 (runs through backward compatibility):
  - `CUSA02084` (US) or `CUSA02901` (EU)
  - Version **02.21** is supported out of the box. Other versions may work if the plugin can find the game's USB imports; otherwise it leaves the game untouched.
- An Xbox 360 guitar (Rock Band or Guitar Hero, third-party ones included).

## Usage

1. Jailbreak the console and load etaHEN.
2. Send the plugin to the console, either way:
   - copy `PS5Instrumentalizer.elf` to `/data/etaHEN/plugins/` and load it from the etaHEN Toolbox, or
   - send it to the ELF loader: `nc -q0 PS5_IP 9021 < PS5Instrumentalizer.elf`
3. Plug in the guitar **before** starting the game.
4. Start Rock Band 4. A notification says **"Xbox 360 guitar support active"** once the game is hooked.
5. Calibrate audio/video latency in the game options with the guitar connected.

Sending the ELF again replaces the running instance. After a console reboot the jailbreak (and this plugin) must be loaded again.

## How it works

The plugin runs as a small background process that watches for Rock Band 4. When the game starts, it injects a tiny hook into the game and redirects its USB calls (`libSceUsbd`) to it. The hook:

1. recognizes an Xbox 360 (XInput) guitar by its descriptor,
2. presents it to the game as a PS3 Rock Band guitar (`12BA:0200`, HID),
3. translates every Xbox 360 input report into the PS3 format the game expects.

Gamepads and other devices are not touched. The original imports are restored when the game or the plugin exits.

## Configuration (optional)

Copy `config.ini.example` to `/data/PS5Instrumentalizer/config.ini`. All keys are optional.

| Key | Effect |
| --- | --- |
| `notifications` | on-screen notifications |
| `whammy_legacy`, `tilt_legacy` | use the old PS4 plugin's axis mapping |
| `extend_length` | report a PS3-sized transfer to the game (default off) |
| `crash_catch`, `trace_log` | debugging aids (default off) |
| `poll_ms` | process check interval |

## Troubleshooting

Everything is logged to `/data/PS5Instrumentalizer/log.txt` (readable over FTP) and to the etaHEN klog (`Klog=1`, port 9081). To investigate a crash, enable `crash_catch = 1` and `trace_log = 1`, reproduce it, and share the log.

## Known limitations

- Drums, keyboards and Guitar Hero Live are not supported.
- The controller LED may keep blinking.
- Plug the guitar in before launching the game.

## Building

Requirements: the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk), plus `clang`, `ld.lld` and `llvm-objcopy` on the host.

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk   # or extract it to ../.deps/sdk/ps5-payload-sdk
make        # builds build/stub.bin and PS5Instrumentalizer.elf
make test   # host-side tests (report translation, stub image, config parsing)
make clean
```

| Path | Contents |
| --- | --- |
| `src/` | daemon: game detection, injection (ptrace + remote mmap), config, logging, crash watcher |
| `stub/` | freestanding hook code that runs inside the game, and the XInput → PS3 translation |
| `include/` | shared headers (stub ABI, XInput, libusb) |
| `tests/` | host tests |

## License and credits

GNU LGPL v2.1 or later (see `LICENSE.txt`), same as the original project.

- OrbisInstrumentalizer by InvoxiPlayGames
- ps5-payload-sdk by John Tornblom and contributors
- etaHEN, kstuff, Relapse and the PS5 homebrew community
