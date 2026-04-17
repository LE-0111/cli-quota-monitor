# Preview
<img width="1707" height="1280" alt="微信图片_20260417094332_99_11" src="https://github.com/user-attachments/assets/6d9f0753-4c99-4b15-8fcd-bcb122fc984f" />

# CLI Quota Monitor

[中文说明](README.zh-CN.md)

CLI Quota Monitor is a local-first ESP32 desk display for monitoring usage windows and quota state from terminal AI tools.

The current version targets a Waveshare `ESP32-S3-RLCD-4.2` device and displays status for:

- Claude Code
- OpenAI Codex CLI
- Gemini CLI

## Features

- local-only architecture, with the ESP32 polling a bridge on your own network
- one dashboard for multiple CLI tools
- device-hosted setup portal for Wi-Fi and bridge configuration
- Arduino IDE firmware workflow
- `Adafruit_GFX`-compatible display abstraction for adapting to other panels

## Architecture

The project has two runtime parts:

- `esp32-firmware/`: Arduino firmware running on the ESP32 display
- `mac-bridge/`: Python bridge running on your computer and exposing local usage data over HTTP

The ESP32 does not call vendor APIs directly. It connects to your Wi-Fi, polls the local bridge, and renders the aggregated result on the display.

The bridge currently reads:

- Claude Code rate-limit data from `/tmp/cc-display-claude.json`
- Codex session usage from `~/.codex/sessions`
- Gemini quota state from `~/.gemini/oauth_creds.json`

## Repository Layout

```text
esp32-firmware/   Arduino firmware for the ESP32 display
mac-bridge/       Python HTTP bridge for local CLI usage data
README.md         English project overview
README.zh-CN.md   Chinese project overview
LICENSE           MIT license
NOTICE            Upstream attribution
```

## Hardware and Software Requirements

Hardware:

- Waveshare `ESP32-S3-RLCD-4.2`

Firmware environment:

- Arduino IDE
- ESP32 board support package
- `ArduinoJson` 7.3.0 or newer
- `Adafruit GFX Library`
- one compatible display driver

Examples already referenced in the code:

- `Arduino_GFX_Library`
- `GxEPD2`
- the included `gfx_waveshare_rlcd.h` adapter

Bridge environment:

- Python 3
- local access to the CLI data sources you want to expose
- for Gemini quota refresh, set `GEMINI_OAUTH_CLIENT_ID` and `GEMINI_OAUTH_CLIENT_SECRET` in the environment before starting the bridge

## Claude Code Helper

`mac-bridge/status-line.sh` is an optional helper for Claude Code. It reads the rate-limit payload from Claude Code's stdin and caches it locally for the bridge.

Example Claude Code config:

```json
{
  "statusLine": {
    "type": "command",
    "command": "/absolute/path/to/mac-bridge/status-line.sh"
  }
}
```

If you change the cache path, update both:

- `mac-bridge/status-line.sh`
- `mac-bridge/bridge_server.py`

## Quick Start

### 1. Start the bridge

```bash
cd mac-bridge
python3 bridge_server.py
```

The bridge listens on `0.0.0.0:8899` by default.

### 2. Flash the ESP32 firmware

Open `esp32-firmware/esp32-firmware.ino` in Arduino IDE, then:

- install the required libraries
- verify the display driver block matches your hardware
- compile and upload

### 3. Configure the device

If Wi-Fi is not configured, the device starts an access point like:

```text
CCDisplay-XXXX
```

Open the setup portal and enter:

- your Wi-Fi credentials
- the LAN IP of the machine running the bridge
- the bridge port, usually `8899`

After saving, the device reboots and starts polling the bridge.

## Bridge Endpoints

- `GET /health`: health check
- `GET /usage`: aggregated usage payload consumed by the ESP32

The bridge binds to `0.0.0.0` by default. If you expose it outside a trusted LAN, you need to add your own network controls.

## Security and Privacy

- intended for trusted local networks
- reads local CLI metadata and quota state from the host machine
- Gemini quota retrieval uses local Gemini CLI OAuth credentials already present on the host
- Gemini token refresh also requires `GEMINI_OAUTH_CLIENT_ID` and `GEMINI_OAUTH_CLIENT_SECRET` to be provided by your local environment
- review `mac-bridge/bridge_server.py` before exposing the bridge to other users or devices

## Project Status

This is a derivative project, not a drop-in continuation of the upstream repository. Major differences include:

- different hardware target
- Arduino IDE workflow instead of the upstream build flow
- local bridge architecture instead of the original cloud-oriented path
- different monitored services
- narrower scope focused on the ESP32 device and local bridge

## Attribution

This project began from code derived from `dorofino/ClaudeGauge` and was then heavily modified for different hardware, tooling, architecture, and monitored services.

See [NOTICE](NOTICE) for attribution details.

## License

Released under the MIT License. See [LICENSE](LICENSE).
