# Test checklist

Do these in order on a new board. Keep the area under the desk clear and a hand near the wall switch.

## A. Before connecting to the desk (board unpowered)

| # | Check | Expected |
|---|---|---|
| A1 | Look the board over | No solder bridges. C6's stripe (−) is on the board-edge side. Both jacks sit flat. |
| A2 | Meter on beep: TP 5V ↔ TP GND | No beep |
| A3 | TP CMD ↔ TP HS | A brief chirp at most. The switch can half turn on from the meter's own voltage. |
| A4 | JP3's two pads | No steady beep (JP3 must be empty) |

## B. Firmware (USB, desk NOT connected)

USB and the desk cable must never be connected at the same time: the desk cable is unplugged before USB goes in, and USB comes out before the desk cable goes back.

| # | Check | Expected |
|---|---|---|
| B1 | Flash `desk.yaml` over USB | Log shows `desk controller firmware 1.4` |
| B2 | **Unplug USB** before connecting the desk cable | |

## C. First power-up on the desk (handset unplugged)

| # | Check | Expected |
|---|---|---|
| C1 | Desk cable into **DESK**, desk on | ESP32 LED on, joins Wi-Fi within about a minute |
| C2 | Meter, black on TP GND: TP 5V, TP 3V3 | About 5 V and 3.3 V |
| C3 | Desk doesn't move by itself | |
| C4 | Home Assistant: Desk height | Shows a sensible height |
| C5 | Meter on TP HS (black on TP GND), then Home Assistant **Up**, then **Stop** | About 3.7 V before, about 0 V while the desk moves (T7 switch open), back to about 3.7 V after it stops |
| C6 | Home Assistant **Up**, then **Stop** | Moves and stops. Last move result: `stopped`. |
| C7 | Home Assistant **Up** and leave it | Stops at the top. "Desk moving" turns off about 1.5 s later. Last move result: `end of travel`. |
| C8 | Same with **Down** | `end of travel` at the bottom |
| C9 | Set **Target height** about 100 mm away, a few times in each direction | The desk stops within a few mm of the target (the first moves may be further off). Last move result: `reached target (… mm)`. |
| C10 | Set **Target height** within 4 mm of the current height | The desk doesn't move. Last move result: `already at target (… mm)`. |
| C11 | Start **Up** from Home Assistant and press **Restart** partway | The desk stops immediately |
| C12 | With the desk asleep, press **Restart** and wait about 10 s | One relay click and no movement. **Desk height** shows the height, **Handset button** shows `idle`, and **Desk moving** stays off. |

## D. With the handset (plug it into **HANDSET**)

| # | Check | Expected |
|---|---|---|
| D1 | Handset Up / Down / preset | Works exactly as without the board. **Desk moving** is on while it moves and turns off about a second after it stops. |
| D2 | Wake the handset | **Handset in use** turns on, and off when its screen goes off. **Handset not responding** stays off. |
| D3 | Home Assistant Up, then press a handset button mid-move | The desk follows the handset immediately. Last move result: `handset took over`. |
| D4 | Wake the handset, then Home Assistant Up | Starts within about 0.2 s, no twitch. Log: `(box already awake)`. |
| D5 | Leave it idle | The box and handset go to sleep as normal (relay clicks off) |
| D5a | Handset preset 1–4 | **Handset button** shows `preset N`, and the desk goes to the saved height |
| D5b | Home Assistant **Preset 1–4**, including once from a sleeping desk | Goes to the saved height and stops by itself. Last move result: `reached preset N (… mm)`. |
| D5c | Home Assistant preset, then **Stop** partway | Stops at once (`stopped`) |
| D5d | Home Assistant preset, then a handset button partway | `handset took over` |
| D6 | Over the next day | No unexpected restarts (Desk going offline, or the handset waking by itself) |

## E. Swapped cables (optional, untested)

This has not been tried on a real desk. It should be electrically safe, because the two jacks are wired pin for pin, but the board isn't expected to work this way round and the control box's reaction is unconfirmed. Skip it unless you're curious.

If you do swap DESK and HANDSET, the design predicts:
- the handset still works
- a Home Assistant move does nothing and reports `desk didn't respond - check the desk and handset cables aren't swapped`

Check **Desk status** stays 0 (48 means E04), then swap them back.
