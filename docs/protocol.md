# Kaidi KDDY094 handset bus

Everything here was measured on a real KDDY094 control box and handset. Wire numbers **T1–T10** are the contact numbers of the 10P10C (RJ50) plug, as wired straight through by the standard handset cable.

## Pinout

| Wire | Signal | Notes |
|---|---|---|
| T1 | (unused / pass-through) | |
| **T2** | **+5 V** from the box | Weak supply: it sags while the box is asleep and under load |
| **T3** | **GND** | |
| T4, T5, T6 | (pass-through) | Not needed for control |
| **T7** | **Commands**, handset → box | 5 V logic, idle high, 9600 baud 8N1 |
| **T8** | **Height**, box → handset | 5 V logic, 9600 baud 8N1 |
| T9 | (pass-through) | |
| **T10** | **Awake** | About 4.5 V while the handset is awake. The box requires it high while receiving commands, or it reports **E04**. |

The board passes T1, T4, T5, T6 and T9 straight through, and puts its own circuits on T2, T3, T7, T8 and T10.

## Frames

Both directions use the same 7-byte frame:

```
68 01 <b2> <b3> <b4> <sum> 16        sum = (01 + b2 + b3 + b4) & 0xFF
```

### Handset → box (T7)

Sent every **13–14 ms** while the handset is awake:

| b2 | Meaning |
|---|---|
| 03 | idle (no button) |
| 01 | up |
| 02 | down |
| 06 / 07 / 08 / 09 | memory preset 1 / 2 / 3 / 4 (b3/b4 may carry the height in mm, low byte first, or be 0; see Presets below) |
| 0A–0C, 17; 0D, 13, 14, 0E, 62, 19 | save preset, limits and settings (see below) |

Examples: `68 01 03 00 00 04 16` idle, `68 01 01 00 00 02 16` up, `68 01 02 00 00 03 16` down, `68 01 06 00 00 07 16` preset 1.

Up/down frames are sent while the button is held. A preset frame is repeated while the desk travels, and **the box drives itself to the preset height and stops**.

### Box → handset (T8)

Sent about every **25 ms** while awake, and for about 12 s after the handset goes to sleep:

```
68 01 <status> <HI> <LO> <sum> 16
```

- **HI LO** = height in mm **above the desk's lowest position** (711 mm / 28.0" on the tested desk). Absolute height = 711 + value. On the tested desk, the top of travel was 440 (1151 mm), matching the box's **S440** label (440 mm stroke legs).
- **status** = 0 normally. **0x30 (48)** while the box shows **E04**. **0x62 (98)** while the sensitivity menu is open (the height field then holds the level).
- Junk bytes sometimes appear between frame bytes, and the height bytes can themselves be 0x16 (heights 21/22 and 276/278). The firmware searches back from each 0x16 for a valid frame rather than assuming fixed positions.

## Waking the box and moving

The firmware copies what the handset does when it wakes a sleeping box:

1. Raise **T10** to about 4.5 V (the board drives it through a PNP transistor from 5 V).
2. Send idle frames for 50 ms, then the command (`01`, `02`, or a preset `06`–`09`) for 100 ms. This "tap" wakes the box and switches on its motor relay.
3. Idle until 1.65 s, then send the command continuously until stop (for a preset: until the box stops by itself at the preset height).
4. Send idle frames for 0.5 s so the box sees the release, then let go of T7 and T10.

If the box is already awake (the board or the handset is holding T10 high), it starts after 200 ms without the tap.

## E04

E04 ("control box malfunction") was seen whenever T10 wasn't held high while commands arrived, e.g. from a loose wire. **Clear it by holding Up + Down on the handset** until the desk lowers to its minimum and completes its rebound. The handset manual warns: **don't let go before it finishes**, or the motor can be damaged permanently. The reset may also clear the handset's saved presets.

## Presets

Saved with **S** then **1–4** on the handset. Pressing a preset sends command 06–09 repeatedly until the desk arrives, then idle again. Two behaviours have been seen:
- An early capture had the preset's height in mm inside the frame (`68 01 06 DB 03 …` = 987 mm).
- On the tested desk in November 2026, the frame carried **no height** (`68 01 06 00 00 07 16`), and the box drove to its own stored height (748 mm) and stopped.

The difference is the handset's **one-touch setting** (hold S until the screen flashes, press 1, then Up/Down):
- **H-1 (one-touch):** tap a preset and the desk goes all the way. The frames carry no height, and the box uses its stored height.
- **H-0 (hold-to-run):** hold the preset button until the desk arrives. The frames carry the target height in mm.

The setting lives in the handset; changing it sends nothing to the box. Either way, the desk stopped at the box's stored height.

**Saving a preset** (S, then 1–4) sends a **single** frame with b3/b4 = 0, and the box stores its current height:

| Handset | Command |
|---|---|
| S + 1 | 0A |
| S + 2 | 0B |
| S + 3 | 0C |
| S + 4 | **17** (confirmed twice; it doesn't follow the pattern) |

S on its own sends nothing. Command 0F has been seen twice, once just after the handset was plugged in. Its meaning is unknown.

## Settings from the handset

These are single frames (b3/b4 = 0 unless noted), sent once when the setting is made. The box stores the setting.

| Handset action (from the manual) | Frames |
|---|---|
| Set lower limit: hold S, then hold 1 until **-L-** | `0D` (at the current height) |
| Set upper limit: hold S, then hold 3 until **-L-** | `13` (at the current height) |
| Clear limits: hold S, then hold 2 until **-C-** | `14`, then `0E` 0.5 s later |
| Anti-collision sensitivity: hold S, press 3 (**G-1** to **G-4**) | `62` opens the menu. The box answers with **status 0x62** and the **current** level in the height field. Then `19` with b4 = the **new** level. |
| cm / inch (S + Down), one-touch H-0/H-1 | nothing sent: handset-only |

While a settings menu is open, the box's frames carry the setting's value where the height normally is (status ≠ 0), so the firmware ignores the height in any frame whose status isn't 0 or 0x30.
