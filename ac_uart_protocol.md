# Reykir 9000 AC / Klima UART Protocol Reference

Reverse-engineered from a Tuya-branded WiFi module sitting between an ESP
(running ESPHome in UART sniffing/passthrough mode) and the Tronitechnik Reykir 9000 AC mainboard.
This is **not** the standard Tuya MCU protocol (which uses a `0x55 0xAA`
two-byte header, variable-length datapoints, and a length field). This is a
simpler, fixed-length, single-byte-header protocol specific to this AC's
mainboard — the Tuya module is just acting as a dumb WiFi bridge over it.

## Physical / UART settings

- Baud rate: matches whatever ESPHome's `uart_debug` capture used (confirm
  against your own `uart:` config — logs did not explicitly state baud/parity,
  but checksum math is consistent with plain 8N1, no parity bit).
- All frames use a trailing single-byte checksum = **sum of all preceding
  bytes in the frame, modulo 256**. This checksum type is confirmed and
  100% reliable across every frame type below.

## Frame types

There are three kinds of frames on the wire, all sharing the same general
22-byte shape (except the 4-byte poll request):

| Direction | Purpose | Length | Example |
|---|---|---|---|
| ESP → MCU | Poll request | 4 bytes | `AA 02 01 AD` |
| MCU → ESP | Poll response (current status) | 22 bytes | `AA 14 01 00 00 01 09 01 02 00 17 1B 00 00 00 00 00 00 00 00 00 FE` |
| ESP → MCU | Set command (change one field) | 22 bytes | `AA 14 02 01 00 01 09 01 02 01 18 19 00 00 00 00 00 0D 21 00 00 2E` |
| MCU → ESP | Set command ack | 22 bytes | `AA 14 02 01 00 01 09 01 02 01 18 19 00 00 00 00 00 00 00 00 00 00` |

### Poll request

Always exactly: `AA 02 01 AD` (fixed, sent on a timer — observed every 5s in
the original Tuya module's behavior. There's no reason your own
implementation can't poll faster or slower).

### Poll response / Set command / Ack — shared 22-byte layout

0-indexed byte positions:

| Byte | Field | Notes |
|---|---|---|
| 0 | Header | always `0xAA` |
| 1 | Length | always `0x14` (20) |
| 2 | Frame type | `0x01` = poll response, `0x02` = set command (and its ack) |
| 3 | Power | `0x00` = off, `0x01` = on |
| 4 | unused | always `0x00` |
| 5 | Vane / Swing | `0x00` = swing (oscillating). `0x01`-`0x05` = fixed vane position, matching the app's on-screen position number 1:1 (confirmed for positions 3 and 5/top; 1, 2, 4 inferred by pattern, not individually tested) |
| 6 | unused / static | always observed as `0x09` — only byte with no confirmed function despite extensive testing |
| 7 | Mode | `0x00` Auto, `0x01` Cool, `0x02` Dry, `0x03` Heat, `0x04` Fan Only |
| 8 | Feature bitmask | Bit `0x01` = Sleep mode on. Bit `0x02` = UVC light on. Bit `0x04` = Mute on. Bit `0x10` = Display on. `0x00` = none active. Persistent flags (not a pulse/toggle) — confirmed to hold across multiple poll cycles. `0x08` unconfirmed, may be unused. Untested whether bits combine (e.g. `0x03` for sleep+UVC together) |
| 9 | Fan speed | `0x00` Auto, `0x01` Low, `0x02` Medium, `0x03` High |
| 10 | Target temperature | direct value in °C, e.g. `0x18` = 24°C |
| 11 | Room (indoor) temperature | direct value in °C, e.g. `0x19` = 25°C |
| 12-16 | padding | always `0x00` |
| 17-18 | unknown, command-frames only | non-zero in `0x02` frames only (e.g. `0x0D 0x21`); resets to `0x00 0x00` in poll responses and most acks. Value drifts slightly between commands but not consistently — likely safe to hardcode `0x00 0x00`, unconfirmed whether MCU requires it |
| 19-20 | padding | always `0x00` |
| 21 | Checksum | sum of bytes 0-20, mod 256 |

## Behavior notes

- **The MCU does not push state changes.** It only reports current status in
  response to a poll (`AA 02 01 AD` → 22-byte `0x01` frame). If a user
  changes settings via the physical remote, you won't know until your next
  poll. Poll on a short interval (5s is proven safe) to stay responsive.
- **The app sends one field-change per command frame**, not a combined
  "set everything" frame. Each `0x02` command frame is a full 22-byte state
  frame with only the *one* changed field different from the last known
  state — all other fields mirror the current state. Recommended: replicate
  this behavior rather than trying to batch multiple changes in one frame
  (untested whether the MCU accepts multi-field changes in one command).
- Every command frame received an ack from the MCU (same `0x02` frame type,
  same field values, valid checksum) within ~100ms, followed shortly by a
  fresh poll response confirming the change had taken effect.
- Enabling **mute** (byte[8] bit `0x04`) has a real side effect: it force-sets
  fan speed (byte[9]) to Low (`0x01`). Disabling mute does **not** revert
  fan speed automatically — it stays at Low until a separate fan-speed
  command is sent. Any ESPHome implementation exposing mute as a
  switch/feature should account for this rather than assuming fan speed is
  preserved.
- Byte[6] (`0x09`) never changed across power, mode (all 5 values), fan
  speed (all 4 values), temperature, vane/swing (multiple positions), UVC
  light, and mute tests. This is the only byte in the frame with no
  confirmed function — treat as static/required, always send as `0x09`.

## Worked examples

Turn AC on (from off, previously Cool/24°C/Low/fixed swing):
```
AA 14 02 01 00 01 09 01 02 01 18 1B 00 00 00 00 00 00 00 00 00 <checksum>
```
(byte[3] = 0x01, all else mirrors last known state)

Switch to Fan Only mode:
```
AA 14 02 01 00 01 09 04 02 01 17 1A 00 00 00 00 00 0D 1D 00 00 <checksum>
```
(byte[7] = 0x04, everything else mirrors last known state)

Set target temperature to 24°C:
```
AA 14 02 01 00 01 09 01 02 01 18 19 00 00 00 00 00 0D 21 00 00 <checksum>
```
(byte[10] = 0x18)

Enable oscillating swing:
```
AA 14 02 01 00 00 09 04 02 01 18 19 00 00 00 00 00 0D 27 00 00 <checksum>
```
(byte[5] = 0x00)

Checksum calculation (Python):
```python
def checksum(frame_bytes):
    return sum(frame_bytes) % 256

# frame_bytes = all bytes except the trailing checksum byte itself
```

## Open items / untested

- Fan speed `0x00` was observed once when switching away from Medium; not
  100% certain it maps to "Auto" vs simply "off/unset" — worth a direct
  confirm if the remote/app has an explicit Auto fan setting.
- Bytes 17-18 in command frames: function unconfirmed. Recommend testing
  with a fixed value (e.g. `0x00 0x00`) to see if the MCU still accepts
  the command before finalizing an ESPHome write implementation.
- Whether the MCU accepts a single command frame with *multiple* fields
  changed at once (untested — all captures so far show one field changed
  per command).
- Whether the bits in byte[8] combine (e.g. `0x03` for sleep+UVC together)
  when multiple features are active simultaneously — untested, each has
  only been confirmed individually.
- Bit `0x08` in byte[8] not yet observed. Given confirmed bits are
  `0x01, 0x02, 0x04, 0x10`, this may simply be unused/reserved rather than
  an undiscovered feature — no longer assumed to necessarily exist.
- AC mode Auto (`0x00`) inferred from the sequence but not directly
  triggered and confirmed via remote/app (Cool, Dry, Heat, Fan Only all
  directly confirmed).
- Vane positions 1, 2, and 4 (`byte[5]`) inferred by pattern from the
  confirmed 1:1 mapping seen at positions 3 and 5 (top) — not individually
  tested.
