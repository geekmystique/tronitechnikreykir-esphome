# reykir_ac_climate — ESPHome external component

Claude AI Generated custom ESPHome `climate` platform implementing the UART protocol documented
in `ac_uart_protocol.md`, for replacing the Tronitechnik Reykir 9000 AC's Tuya WiFi module entirely
with an ESP running ESPHome. The protocol was derived with help of LLM by piggybacking on the original Tuya Wifi stick.

## Files

- `components/reykir_ac_climate/climate.py` — Python config schema
  (`climate:` platform + four `switch:`-style config blocks for sleep, UVC,
  mute, display).
- `components/reykir_ac_climate/reykir_ac_climate.h` / `.cpp` — C++
  implementation: polls the AC every `update_interval`, parses 22-byte
  status frames, and sends one-field-at-a-time set commands mirroring the
  behavior observed from the original app.
- `example.yaml` — minimal working config to adapt to your device.

## What this does

- Exposes power, mode (Auto/Cool/Heat/Dry/Fan Only), fan speed
  (Auto/Low/Medium/High), target temperature, current (room) temperature,
  and swing (oscillating vs. fixed) as a standard Home Assistant/ESPHome
  `climate` entity.
- Exposes sleep mode, UVC light, mute, and display as separate switches,
  matching the confirmed bits in byte[8].
- Polls on a timer (default 5s, matching the original module's behavior)
  since the AC mainboard never pushes state changes unsolicited — this
  also means changes made via the physical remote will only show up after
  the next poll.
- Sends set-commands as full mirrored 22-byte state frames with only the
  targeted field changed, same pattern the original app used, rather than
  guessing at combined multi-field writes (untested whether the MCU
  accepts those).