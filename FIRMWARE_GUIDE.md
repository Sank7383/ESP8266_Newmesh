# ESP8266 Nurse-Call Firmware — Complete Reference Guide

> **Purpose of this document**: a single, self-contained reference for this firmware's settings, data flow, and internal mechanisms — written so a human reviewer *or* an AI assistant can build a complete, accurate mental model without needing to read every source file first. Every claim below is grounded in an exact file/line in this repository as of the time this was written; if the code changes, this file should be updated alongside it. Where something is a known gap rather than a working feature, it is labeled **GAP** explicitly rather than glossed over.

---

## 1. What this project is

A single ESP8266 Arduino firmware image for a "nurse call" device (hospital/care-facility call button + status LED + door indicator), designed so that **hardware variant, network route, and behavior rules are all chosen at runtime** via a settings form — not by compiling separate `.bin` files per variant (which is how the legacy predecessor firmware worked).

Three independent axes are runtime-selectable:
- **Route** (how status reaches the central server): WiFi mesh (ESP-NOW + WebSocket/Socket.IO), RS485, or Ethernet.
- **Button hardware**: 74HC165 shift-register (5-button or restricted-to-3-button), or 2 direct GPIO pins (two slightly different wiring variants).
- **Device role**: Bed unit, Toilet pull-cord, Door indicator, or a combined Bed+Toilet unit.

All configuration lives in one struct, `DeviceConfig myConfig` (`NurseCallConfig.h`), persisted to EEPROM, and changed via a settings-form protocol running over the device's own local WebSocket server (port 81).

## 2. File map

| File | Role |
|---|---|
| `ESP8266_Newmesh.ino` | Composition root: `setup()`/`loop()`. Owns the global runtime state (`g_callStatus`, `g_roomAggregator`, `g_buzzer`) and the button→state-machine→transport dispatch. |
| `NurseCallConfig.h` / `.cpp` | The `DeviceConfig` struct (all persisted settings + runtime-variant selectors), all enums, EEPROM load/save/defaults. |
| `DeviceProtocol.h` / `.cpp` | The settings-form protocol (`sendFormData`/`getFormData`, forms 0–14), the nurse-call message dispatch (`decodeString`), status snapshot, restart/OTA-stub. |
| `CallStateMachine.h` / `.cpp` | Pure, hardware-free call-status state machine. No I/O — takes a status + a button action, returns a new status. |
| `IButtonInput.h` / `DebounceEngine.h` | Abstract button interface + shared debounce logic used by every button driver. |
| `ButtonInputShiftReg5.*`, `ButtonInputGpio2.*`, `ButtonInputGpio3Remote.*`, `ButtonInputFactory.*` | Concrete button-hardware drivers and the factory that picks one at boot based on `myConfig.buttonVariant`. |
| `ILedController.h`, `LedStripController.*`, `BlinkController.h`, `LedAggregator.h` | LED color/blink logic and multi-room status aggregation. |
| `ITransport.h`, `TransportMeshWifi.*`, `TransportRs485.*`, `TransportEthernet.*`, `TransportFactory.*` | The uplink-to-server abstraction and its three concrete implementations, picked at boot based on `myConfig.routeType`. |
| `LocalAccessStack.*` | The always-on local AP + config webserver + WebSocket server — independent of which route is active. |
| `MYESPNOW.h` | Vendored ESP-NOW mesh packet layer (packet struct, RX ring buffer, sequence dedup). Included exactly once, only from `NewMeshNOW.h`. |
| `NewMeshNOW.h` | Vendored WiFi AP/STA bring-up, WebSocket server/client, Socket.IO client, the settings-form wire-protocol plumbing (`decodeString1`/`decodeString2`), uplink connect/reconnect (`connectUplink`, `uplinkHealthCheck`), and the `/forms` browser UI (embedded HTML/JS). Included exactly once, only from the `.ino`. |
| `MeshNowExports.h` | `extern`-only declarations of everything `NewMeshNOW.h`/`MYESPNOW.h` define, so every OTHER `.cpp` file can call into the mesh/network layer without re-including those single-inclusion files. |
| `BuzzerController.h` | GPIO5 buzzer, triggered on button press for audible feedback. |

## 3. How the settings protocol works (the wire format)

Settings are **not** a normal HTML form. A client connects to the device's own WebSocket server on **port 81**, and the device pushes/receives plain text messages in a custom grammar:

- On connect, the device sends `"Connected:<chipID>:1"`.
- The client sends any command as **plain text, no address prefix needed** if connected directly to the device's own server (`NewMeshNOW.h`'s `decodeString1` only requires a `"<chipID>="` prefix for *relayed/mesh-origin* traffic, not direct local connections).
- `"Test"` → device replies with the root menu (form 0).
- `"RESPONSE:<formid>|<formid> <argk>|<urlencoded_value> <argk>|<urlencoded_value> ..."` → submits a form. The **first token's key becomes the form ID** (its value is ignored); every subsequent token is `argk|value` for that form.
- The device replies with `"FORM:<id>$NEXT$<title>..."` or `"FORM:<id>$DONE$<title>..."` followed by field tokens:
  - `$T|<label>||<urlencoded_value>|<flags>` — a text field.
  - `$D|<label>|<opt1_label>:<opt1_value>#<opt2_label>:<opt2_value>...|<current_value>|<flags>` — a dropdown.
  - `$R|<label>|<opt1>:<val1>#...|<current>|<flags>` — used only for the root menu (form 0).
- **Field order is the contract.** The Nth field sent by the device corresponds to `argk = N-1` when the client submits it back. Appending new fields at the end of a form is safe; reordering or removing existing fields breaks anything that already assumes the old order.
- There is a **1-second duplicate-message suppression** in `sendToAll()` (`NewMeshNOW.h`) — sending the exact same text twice within 1 second silently drops the second one.
- A minimal browser UI that speaks this protocol is embedded directly in the firmware and served at `http://<device-ip>/forms` (see `FORMS_PAGE` constant in `NewMeshNOW.h`).

## 4. Complete settings-form reference

Every field below is listed with: the form/page it's on, its `argk` index (the position used when submitting), the exact `DeviceConfig` field it reads/writes, and what it actually does. Source of truth: `DeviceProtocol.cpp`'s `sendFormData()` (builds the field list, in order) and `getFormData()` (parses `argk` back into config).

### Form 0 — Root menu
Not a data form — a `$R|` selector routing to every other form: `Network:1 Device:2 LED:3 RS485:4 Mesh:5 Info:7 Role and Route:11 Buttons:12 Ruleset:13 Debug:14 Reboot:20 Factory Reset:21`. Selecting `20` calls `ESPrestart()` immediately; selecting `21` calls `configApplyDefaults()` + `configSave()` + `ESPrestart()` (full factory reset).

### Form 1 — Network Settings
| argk | Field | Config path | Effect |
|---|---|---|---|
| 0 | SSID | `myConfig.mySSID` | Upstream WiFi network this device's STA radio tries to join. Empty = device stays AP-only, never attempts an uplink. |
| 1 | Password | `myConfig.myPass` | Password for the above SSID. |
| 2 | Company Code (asccode) | `myConfig.asccode` | Site/company identifier. Used in the AP SSID (`NETE<asccode><machineid>` / `MESH<asccode><machineid>`), in Socket.IO payloads, and in the Mesh ID scheme. |
| 3 | Device ID | `myConfig.machineid` | This device's numeric identity. Used in the AP SSID, as the `rid` in status messages sent to the server, and as the RS485 bus address. |
| 4 | Uplink Protocol | `myConfig.socketio` (bool, 0/1) | `0`=WebSocket, `1`=Socket.IO. Selects which client (`webSocketClient` vs `webSocketIo`) and which wire format (`"s<id>,<state>,<doorid>"` vs a JSON `update_status` event) is used to report status — see §7. |
| 5 | Server Host | `myConfig.myServer` | Central server address for the uplink. **Must be non-empty** or the uplink never attempts to connect at all (this was previously bugged with an arbitrary 11–19 character length requirement — fixed, now just requires non-empty). |
| 6 | Server Port | `myConfig.myPort` | Port for the above. |
| 7 | Static IP (blank = DHCP) | `myConfig.myIP` | Applied via `WiFi.config()` once STA connects to `mySSID` specifically (`NewMeshNOW.h`'s `gotIpEventHandler`). |
| 8 | Gateway | `myConfig.myGateway` | Paired with the static IP above. |
| 9 | Subnet Mask | `myConfig.myNetmask` | Paired with the static IP above. |

### Form 2 — Device Settings
| argk | Field | Config path | Effect |
|---|---|---|---|
| 0 | Device Name | `myConfig.myDeviceName` | Cosmetic device label. |
| 1 | Toilet ID | `myConfig.toiletid` | Used to derive `bedToiletShareUnit()` — `true` when `toiletid == 0` **or** `toiletid == machineid` (0 means "no separate toilet unit configured"). Gates both the call-legality rule in §6 AND the toilet-call routing/LED-zone behavior in §8/§9 below — a non-zero `toiletid` that differs from this device's own `machineid` means the physical toilet pull-cord wired into this device reports under that OTHER device's identity, not this one's. |
| 2 | Door Indicator ID | `myConfig.doorIndicatorId` | Sent as the `"doorid"` field in the plain-WebSocket status message format (`"s<id>,<state>,<doorid>"`). |
| 3 | Allow Reboot | `myConfig.allowReboot` | Stored but **not currently read anywhere** — **GAP**: no code branch checks this flag before allowing a reboot. |

### Form 3 — LED Settings
| argk | Field | Config path | Effect |
|---|---|---|---|
| 0 | Color Row | `myConfig.color_row_indi` (also mirrored into `color_row_frnt`/`color_row_toi`, which are otherwise unused legacy fields) | `0`=RGB, `1`=GRB, `2`=BRG — selects which row of the `LED_COLOUR` table compensates for physical LED-strip wire order (see §8). **Defaults to `1` (GRB)**, since that's the native wire order of the overwhelming majority of real WS2812/WS2812B strips — switch to `0` only if bench-testing shows a genuinely RGB-native strip. |
| 1 | LED Brightness | `myConfig.ledBrightness` | Passed to `FastLED.setBrightness()`. |
| 2 | Default LED On | `myConfig.default_led` | `On`: idle (nothing active) shows a steady `IDLE_ON` color. `Off` (default): idle is normally dark, pulsing `IDLE_ON` briefly every argk-4's interval as a heartbeat. See §8. |
| 3 | Active LED Count | `myConfig.ledCall.count` (mirrored into `ledToilet.count`/`ledAggregate.count`) | How many LEDs of the strip are actually painted (see §8 for zone layout). |
| 4 | Idle Blink Interval Sec | `myConfig.Indicator_timer` | Seconds between idle heartbeat pulses when Default LED On is Off. Rounded to the nearest multiple of 5, minimum 5, on save. Defaults to `10`. (Reinterpreted from the legacy field's confusing `*10000ms`-per-unit scaling — this rewrite stores it as plain whole seconds.) |
| 5 | Toilet Idle Indication | `myConfig.toiletIndicationOnIdle` | `On` (default): `leds_[1]` (the toilet zone, see §8) shows `idleColorRaw()` like every other zone when the toilet has no active call. `Off`: `leds_[1]` stays fully dark whenever the toilet has no active call, regardless of Default LED On/heartbeat. Only affects the no-call idle display — an actual active toilet call always shows either way. |

**All five fields in this form take effect live, without a reboot** — `LedStripController::refreshConfig()` re-reads them from `myConfig` every loop iteration (see §8). This is true only for Form 3; most other settings (route, button variant, PCB revision, etc.) are still captured once at boot and need a reboot (Form 0 → Reboot) to take effect.

### Form 4 — RS485 Settings
| argk | Field | Config path | Effect |
|---|---|---|---|
| 0 | New 485 Module | `myConfig.new485Module` | Stored but **not currently read anywhere** in the new RS485 transport — **GAP** (was meaningful in the legacy firmware's SoftwareSerial-based implementation, which this rewrite replaced with a hardware-UART implementation; see §7). |
| 1 | Legacy Protocol Select | `myConfig.newProtocol` | Stored but **not currently read anywhere** — **GAP**, same reason as above. |
| 2 | Baud Rate | `myConfig.rs485Baud` (dropdown index mapped to 9600/4800/19200) | Actually used — sets the hardware `Serial.begin()` baud rate in `TransportRs485::begin()`. |

### Form 5 — Mesh Settings
| argk | Field | Config path | Effect |
|---|---|---|---|
| 0 | Node ID | `myConfig.nodeId` | ESP-NOW mesh node identity (distinct from `machineid`). |
| 1 | Mesh ID | `myConfig.meshId` | ESP-NOW network identifier — packets with a different `meshId` are ignored/relayed differently (see `MYESPNOW.h`'s `processIncomingPackets`). |
| 2 | WiFi Channel | `myConfig.wifiChannel` | Used for both the ESP-NOW radio and the AP's channel. |
| 3 | Tx Time | `myConfig.txTime` | Minimum value clamped to 2000 in `configApplyDefaults`; stored but its only read site is a floor-check in `espnow_setup()` (`MYESPNOW.h`). |
| 4 | Mesh Enabled | `myConfig.mesh_en` | Gates whether `esp_now_init()` runs at all in `espnow_setup()`. |
| 5 | Retransmit | `myConfig.retransmit` | Legacy field, minimal current usage. |

### Form 7 — Info
Read-only. One `$T|Status||...|0` field built by `getCurrentStatus1()`: device name, ID, role, route, current reported status code, housekeeping flag, upstream-connected flag, free heap, uptime.

### Form 11 — Device Role and Route
| argk | Field | Config path | Effect |
|---|---|---|---|
| 0 | Device Role | `myConfig.deviceRole` | `0`=Bed, `1`=Toilet, `2`=DoorIndicator, `3`=Combo. **Critically**: only roles 0/1/3 show this device's own call-color on the LED strip — role 2 (DoorIndicator) shows *only* the aggregate/room-priority color, ignoring this device's own state entirely (see §8). |
| 1 | Route Type | `myConfig.routeType` | `0`=WiFi mesh, `1`=RS485, `2`=Ethernet. Selects which `ITransport` implementation `TransportFactory::create()` returns at boot (requires reboot to take effect). |

### Form 12 — Button Configuration
| argk | Field | Config path | Effect |
|---|---|---|---|
| 0 | Button Variant | `myConfig.buttonVariant` | `0`=None, `1`=Shift5, `2`=Shift3, `3`=Gpio2, `4`=Gpio3Remote. Selects which `IButtonInput` concrete driver `ButtonInputFactory::create()` returns (requires reboot). **Shift5 and Shift3 run the identical driver/code path** — "Shift3" is not different hardware, it's Shift5 with 4 of the 7 slot mappings below set to `None`. |
| 1 | PCB Revision | `myConfig.buttonPcbRevision` | Only consulted by the Shift5/Shift3 driver — picks which of two known 74HC165 bit-pattern tables to decode against (`Old` vs `NewSticker`). Silently ignored by Gpio2/Gpio3Remote. |
| 2–8 | Slot Cancel / Call / Toilet / Extra / Blue / Attend / AP | `myConfig.buttonMap.slot[0..6]` | Maps a physical input position to a `ButtonAction`. See §5 for the full mechanism — **on Gpio2/Gpio3Remote, only slots 0 and 1 (Cancel/Call) are ever read; slots 2–6 are configurable here but have no effect.** |

`BUTTON_ACTION_OPTIONS` (the dropdown choices for slots 2–8): `None:0 Call:1 Cancel:2 Toilet:3 Extra:4 CodeBlue:5 Housekeeping:6 Palm:7 ApMode:8 Care:9`.

### Form 13 — Call Ruleset
| argk | Field | Config path | Effect |
|---|---|---|---|
| 0 | Ruleset Preset | `myConfig.ruleset.preset` | **GAP — stored but never read anywhere in `CallStateMachine.cpp`. Currently has zero effect on behavior.** Intended to let a preset auto-fill fields 1–3 below; not wired up. |
| 1 | Housekeeping Bypasses Care | `myConfig.houseKeepings` (note: a top-level `DeviceConfig` field, not nested under `.ruleset`) | Whether Extra-Help/Code-Blue can be triggered directly from an active Call/Toilet-Call, skipping Care. Read at `CallStateMachine.cpp` lines 33, 38, 77, 81. |
| 2 | Care Required Before Cancel | `myConfig.ruleset.careRequiredBeforeCancel` | Whether a bare Call can be cancelled directly (false) or must go through Care first (true). **Does not affect Toilet-Call, which can always be cancelled directly** (hardcoded, line 24). Read at line 73. |
| 3 | Direct Code Blue From Idle | `myConfig.ruleset.directCodeBlueFromIdle` | Whether Code-Blue works from Idle with nothing active (emergency override). Read at line 60. |
| 4 | Custom State Enabled | `myConfig.ruleset.customState.enabled` | Enables the reported-code override for `CallState::CUSTOM` (see §6). |
| 5 | Custom State Reported Code | `myConfig.ruleset.customState.reportedCode` | What number is sent to the server while in the custom state. |
| 6 | Custom State LED Color Index (0-7) | `myConfig.ruleset.customState.ledColorIndex` | Which `LedColorSlot` table column to paint for the custom state. |

**GAP**: nothing currently sets `status.mainState = CallState::CUSTOM` — there is no button action or trigger wired to reach it. Fields 4–6 configure what *would* happen if something did.

### Form 14 — Debug
Read-only, one compact multi-line `$T|Debug||...|0` field (rendered as a `<textarea>` in `/forms`). Submitting it just re-sends a fresh snapshot rather than persisting anything — it's a point-in-time pull, not a live stream. Content: WiFi SSID/RSSI/STA-IP/gateway, AP SSID/IP/client-count/bridged-or-standalone, uplink target/connected/last-success-time/fail-count, route/amServer/ethernet/espnow flags, current call state/toilet/housekeeping/role, LED link status (networkUp/serverUp), heap/uptime.

### Live debug log (separate from Form 14)
`debugdata(msg)` (declared in `MeshNowExports.h`, defined in `NewMeshNOW.h`) broadcasts a `"DBG:" + msg` text frame to every connected WebSocket client in real time — this is how `ESP8266_Newmesh.ino` traces button press/release/state-machine results, and how `LedStripController::logPixels()` (see §8) dumps every pixel's actual RGB hex value on each repaint. **The `/forms` page renders this live** in a "Live Debug Log" panel below the settings form (added specifically so `logPixels()` output is visible without a separate raw-WebSocket tool) — `handleMessage()` intercepts anything prefixed `DBG:` and appends it to the `#log` div instead of trying to parse it as a form. The panel persists across form navigation (it lives outside the `#app` div that `renderForm()` replaces) and caps at 300 lines.

## 5. Button input & the "slot" mapping mechanism

`myConfig.buttonMap.slot[0..6]` is a plain 7-element array of `ButtonAction`. **A "slot" is nothing more than an array index** — the labels ("Cancel", "Call", "Toilet"...) describe the traditional physical position on the 74HC165 board layout, not anything enforced by the firmware. Two independent steps determine what happens on a button press:

**Step 1 — hardware signal → slot number** (differs per driver):
- **ShiftReg5/Shift3** (`ButtonInputShiftReg5.cpp`): the 74HC165 latches 8 physical lines into one byte, read via `shiftIn()`. Each of the 7 real buttons pulls a distinct bit low, producing a unique byte pattern. `decodeSlot()` masks out the palm-accessory bit (bit 6) and linearly searches a 7-entry table (`BUTTON_ARRAY_NEW_STICKER` or `BUTTON_ARRAY_OLD_PCB`, chosen by `buttonPcbRevision`) for a match; the matching table index **is** the slot number. No match = no button currently pressed.
- **Gpio2/Gpio3Remote**: no table, no decode step — `logicalKey` is hardcoded: `0` if the cancel-wired pin reads LOW, `1` if the call-wired pin reads LOW. The physical pin assignments themselves are fixed per driver (Gpio2: cancel=pin2, call=pin4; Gpio3Remote: cancel=pin16, call=pin2) and are **not** configurable.

**Step 2 — slot number → action** (identical code for every driver): `ButtonAction action = map_.slot[slotIdx];` — a plain array lookup into whatever Form 12 configured. This is the only place user configuration enters the picture; the physical-to-slot-number mapping in Step 1 is fixed by wiring/hardware and cannot be changed via settings.

**Debounce and press/release timing** (`DebounceEngine.h`, shared by all drivers): a candidate slot value must read stable for `stableMs` (default 50ms) before being accepted. Once accepted, `DebounceResult::PRESSED` fires **immediately** (used for instant buzzer feedback) with the action already known. When the button is released and stable again, `DebounceResult::RELEASED` fires with `isLongPress` computed from hold duration (used for the actual `CallStateMachine::apply()` call, which needs long/short-press to be known already — see §6). A physical press therefore produces **two** `ButtonEvent`s from `poll()`, distinguished by `ButtonEvent.type` (`ButtonEventType::PRESSED` / `RELEASED`).

`PALM_ATTACHED` is a special case on the ShiftReg5 driver only: it fires immediately on a raw bit-6 change, bypassing the debounce/slot mechanism entirely (it's an accessory-presence signal, not a button).

## 6. Call state machine (`CallStateMachine.h`/`.cpp`)

**Representation** — deliberately not bit-packed. A `CallStatus` struct holds three independent fields:
```cpp
struct CallStatus {
  CallState mainState = CallState::IDLE;   // IDLE/CALL/CARE/EXTRA_HELP/CODE_BLUE/CUSTOM
  bool toiletCallActive = false;
  bool housekeeping = false;
};
```

**`CallState` values and what they mean**: `IDLE=0, CALL=1, CARE=2 (staff acknowledged/attending), EXTRA_HELP=3, CODE_BLUE=4, TOILET_CALL=5 (only ever used as a *reported code*, never as mainState — see below), CUSTOM=6 (unused extension point, see Form 13), HOUSEKEEPING=7, HOUSEKEEPING_CALL=8`.

**`apply(status, action, isLongPress, bedToiletShareUnit, ruleset, housekeepingBypassesCare)`** — the one function that mutates state. Key rules, each cross-referenced to `CallStateMachine.cpp`:
- `HOUSEKEEPING` action (line 12): always legal. Short press sets the housekeeping flag, long press clears it. Independent of everything else.
- A pending toilet call with `mainState==IDLE` (lines 23–44) is its own mini-ladder: Cancel always clears it directly; Care/Extra/Blue escalate it into `mainState` exactly like an active Call would (Extra/Blue only if `housekeepingBypassesCare`).
- `IDLE` (line 48): `CALL` → `CALL`. `TOILET_CALL` → sets `toiletCallActive` (blocked if `bedToiletShareUnit && activeCallInProgress`). `CODE_BLUE` → `CODE_BLUE` only if `ruleset.directCodeBlueFromIdle`.
- **`TOILET_CALL` never even reaches `apply()` when `!bedToiletShareUnit(myConfig)`** — see the `.ino`'s button-dispatch block, which intercepts it first. When the toilet is a genuinely separate logical device (non-zero `toiletid` different from this device's own `machineid`), the physical pull-cord input on this bed unit's shift register reports a plain `CALL(1)` status directly under `toiletid` via the `.ino`'s local `sendToiletStatus()` helper (`StatusPayload{deviceId: myConfig.toiletid, statusCode: 1}`, plus a matching `g_roomAggregator.updateRoom(toiletid, 1)` so this bed's own LED zone reflects it immediately) — `g_callStatus`/`toiletCallActive` on THIS device are never touched for that case. `TOILET_CALL(5)` as a *reported code* is reserved for the `bedToiletShareUnit` case, where the toilet has no separate identity and its state folds into this device's own combined report instead.
- **`CANCEL` also clears the separate toilet id when `!bedToiletShareUnit(myConfig)`** — after `apply()` runs its normal cancel logic on this bed's OWN `g_callStatus` (per the rules above), the `.ino` separately calls `sendToiletStatus(IDLE=0)` for `myConfig.toiletid`, unconditionally (not gated on whether the bed's own cancel was `legal` — the toilet clear isn't subject to this bed's cancel ruleset). This is what makes one physical Clear/Cancel button reset both the bed AND a separate toilet unit's status on the server, matching the `TOILET_CALL` send it's paired with. There is currently no equivalent "clear" wired for any hardware-level toilet-cord *release* signal specifically (only the room-wide Cancel button triggers it) — see §9.
- `CALL` (line 66): `CARE` → `CARE`. `CANCEL` → `IDLE` only if `!ruleset.careRequiredBeforeCancel`. `EXTRA_HELP`/`CODE_BLUE` → escalate only if `housekeepingBypassesCare`.
- `CARE` (line 87): `EXTRA_HELP`/`CODE_BLUE` always escalate from here (no ruleset gate). `CANCEL` → `IDLE`.
- `EXTRA_HELP` (line 102): `CODE_BLUE` escalates. `CANCEL` → `IDLE`.
- `CODE_BLUE` (line 113): only `CANCEL` → `IDLE`.
- `CUSTOM` (line 120): only `CANCEL` → `IDLE`. Nothing currently transitions *into* CUSTOM (see Form 13 gap).

**`reportedStatusCode(status, ruleset)`** — the single number sent to the server, computed fresh each time, never stored:
- Housekeeping set + any active call/toilet-call → `8` (HOUSEKEEPING_CALL).
- Housekeeping set + nothing active → `7` (HOUSEKEEPING).
- `mainState == CUSTOM` and enabled → `ruleset.customState.reportedCode`.
- Otherwise → the numeric value of `mainState` if not IDLE, else `5` (TOILET_CALL) if a toilet call is active, else `0` (IDLE).

## 7. Transport / uplink (device → server)

All three implement `ITransport` (`begin`, `loop`, `sendStatus`, `isLinkUp`, `isNetworkUp`), selected by `TransportFactory::create()` based on `myConfig.routeType`. **Only the status-uplink leg is route-specific** — the local AP/webserver (`LocalAccessStack`) always runs regardless.

`isLinkUp()` reflects the specific configured uplink protocol being connected right now (WebSocket/Socket.IO connected, RS485 polled within the last 60s, Ethernet PHY linked). `isNetworkUp()` is the coarser tier below that — WiFi STA associated / Ethernet linked — true for `TransportMeshWifi` even if the server itself is unreachable. RS485/Ethernet have no separate tier below their own protocol, so their `isNetworkUp()` just mirrors `isLinkUp()`. Both feed `ILedController::setLinkStatus()` every loop (see §8) — this is what drives the disconnect blink.

**`TransportMeshWifi`** (route 0, default): brings up ESP-NOW (`espnow_setup()`) if `mesh_en`. `sendStatus()` always sends a local `"j<id>,<code>"` mesh broadcast (feeds other devices' `LedAggregator`, unrelated to the external server), then separately reports to the actual server:
- **WebSocket mode** (`socketio=false`): `"s<id>,<code>,<doorIndicatorId>"` via `webSocketClient.sendTXT()`, only if connected.
- **Socket.IO mode** (`socketio=true`): `["update_status",{"asccode":<n>,"r":<id>,"door":0,"palm":0,"s":<code>,"drip":0,"type":3}]` via `webSocketIo.sendEVENT()`; falls back to sending the same JSON as raw WebSocket text if only `webSocketClient` is connected.

Connection lifecycle lives in `NewMeshNOW.h`: `connectUplink()` picks mesh-gateway vs `myServer` vs Socket.IO based on current SSID/config, `uplinkHealthCheck()` (called every ~5s from `myrun()`) verifies the relevant client is connected, retries every 3rd failed check, calls `resendCurrentStatus()` once reconnected, and force-restarts the device after 12+ consecutive failures past 120s uptime. **Both `webSocketClient.loop()` and `webSocketIo.loop()` must run every cycle** for either client to ever progress its connection state machine — this was previously missing for `webSocketIo` and is why Socket.IO appeared permanently "down."

**`TransportRs485`** (route 1): half-duplex, polled-slave protocol over the hardware UART (not SoftwareSerial, unlike the legacy firmware). Listens for a poll byte whose high nibble matches `machineid & 0x0F`; replies with a 4-byte CRC16-framed status frame. `sendStatus()` only caches the value (`pendingStatusCode_`) — the actual bus write happens on the next matching poll, since this is a slave, not a push protocol.

**`TransportEthernet`** (route 2): ENC28J60 over SPI, static IP from `myConfig.myIP/myGateway/myNetmask`. On successful link, sets the shared `ethercon` flag and explicitly disconnects WiFi STA (Ethernet fully replaces WiFi, no fallback). `sendStatus()` sends the same `"j"` local-broadcast format via `sendToAll()`, no Socket.IO/RS485-specific framing.

## 8. LED color system (`LedStripController.cpp`)

Zone layout, decided by `myConfig.deviceRole`:
- **DoorIndicator (role 2)**: the *entire* strip shows one color — the aggregate/room-priority color from `LedAggregator`. This device's own `CallStatus` is never shown on its LEDs.
- **Bed/Toilet/Combo (roles 0/1/3)**: three distinct zones on the one physical strip, matching how a bed unit's shift-register PCB is actually wired (bed button + toilet pull-cord + door-indicator relay, all on one device, no separate MCU per zone):
  - `leds_[0]` — this device's OWN call status (`lastStatus_.mainState`), **plus** a mirrored toilet-call color when `bedToiletShareUnit_` is true (`toiletid` 0 or equal to this device's own `machineid`) and `toiletCallActive` — same physical unit, same `CALL_RED` color as the bed itself would show, since there's no separate toilet device to display it on instead. This is purely a LOCAL LED effect: `reportedStatusCode()` (§6) still reports the distinct `TOILET_CALL(5)` to the server, never folded into `CALL(1)` — only the LED mirrors. A distinct (non-shared) `toiletid` never affects `leds_[0]`; that case is `leds_[1]`-only, below.
  - `leds_[1]` — the TOILET's status:
    - `bedToiletShareUnit(myConfig)` true (`toiletid` is 0 or equals this device's own `machineid`): derived from this same device's `lastStatus_.toiletCallActive` flag (steady `CALL_RED` when active, else idle) — same source `leds_[0]`'s mirror above reads from.
    - `bedToiletShareUnit` false (a distinct `toiletid`): derived from `toiletRemoteStatus_`, set every loop from `LedAggregator::statusFor(myConfig.toiletid)`. **Deliberately narrower than the aggregate zone below**: this only ever changes from (a) this bed unit's OWN button routing (`sendToiletStatus()` in the `.ino` — `Call(1)` on the toilet pull-cord, `Idle(0)` on Cancel, see §6) or (b) a genuine `"j<toiletid>,<status>"` report arriving FROM the toilet's own device reporting its own button event. It is not influenced by the generic "highest status across every other room" computation that drives the aggregate zone — a status change in some unrelated room can't move this zone. Colored via `colorSlotForStatusCode()` (maps a raw reported number to a color, same names as `colorSlotForCallState()`). When there's no active call, shows `idleColorRaw()` or stays fully dark depending on `myConfig.toiletIndicationOnIdle` (Form 3, argk 5).
  - `leds_[2..count-1]` — the aggregate/door-indicator zone: highest-priority raw status code (`aggregateMaxStatus_`) across every room `LedAggregator` knows about (this DOES intentionally reflect arbitrary mesh/server "j" traffic for any room, unlike `leds_[1]` above), colored the same way as `leds_[1]`'s remote case.

Color resolution (`repaint()`): `leds_[0]` is considered "active" (`bedZoneActive`) when `mainState != IDLE` OR (`bedToiletShareUnit_` AND `toiletCallActive`) — the mirror case above. If `housekeeping` is set and the zone is active, the LED **blinks** (500ms, `BlinkController`) between the housekeeping color (`HK_PINK`) and the specific active-call color — richer local feedback than the single collapsed "8" code the server sees. Otherwise it's a steady color: `CALL`→red, `CARE`→green, `EXTRA_HELP`→orange, `CODE_BLUE`→blue, `CUSTOM`→whatever `ruleset.customState.ledColorIndex` points to, a mirrored toilet call→red (same as `CALL`), `IDLE`→`idleColorRaw()` (see "Default LED On" below, not a fixed color). `leds_[1]`/aggregate zones are steady colors only — no blink.

`colorRow` (0=RGB/1=GRB/2=BRG, from Form 3, **defaults to 1/GRB**) selects which row of a 3×9 color table to use, compensating for physical LED-strip wiring order while FastLED itself is always told `RGB`. Getting this wrong doesn't turn LEDs off or freeze them — it swaps *which physical channel* a color's bytes land on, so e.g. row 0 on a GRB-native strip (the common case) shows `CALL_RED` as green and `CARE_GREEN` as red, a clean R/G swap, while everything still updates live. `logPixels()` (below) is the way to tell the two apart: if the debug log shows the expected hex (`#FF0000` for a call) but the strip visibly shows a different color, it's this row setting, not a call-state bug — the row values in the table are a starting point, not hardware-measured, so if none of the 3 rows produce the right color on a given strip, retune the specific slot's hex in `LED_COLOUR` directly and confirm against `logPixels()`.

**Default LED On / idle heartbeat (`idleColorRaw()`, `IDLE_ON` slot)**: whenever nothing is active (own call zone idle, or a door indicator's aggregate zone idle), the color shown is NOT a fixed "off" — it's `idleColorRaw()`:
- `myConfig.default_led == true`: steady `IDLE_ON` (yellow) — device always shows it's alive.
- `myConfig.default_led == false` (default): normally `CLEAR` (off), except for a brief `IDLE_ON` pulse (`IDLE_PULSE_ON_MS` = 300ms) every `myConfig.Indicator_timer` seconds (default 10, Form 3 argk 4) — a heartbeat so the device doesn't look dead while genuinely idle. The pulse timer (`idleTimerMs_`/`idlePulseOn_` in `tick()`) runs continuously regardless of call state; it only becomes visible when `idleColorRaw()` is actually what's being painted, so it never interferes with an active call's color.

**Live settings refresh (`refreshConfig()`)**: `LedStripController`'s Form-3-relevant fields (`colorRow_`, `activeCount_`, `role_`, `defaultLedOn_`, `idleHeartbeatIntervalMs_`, brightness) are re-read from `myConfig` every loop iteration via `g_ledController->refreshConfig(myConfig)` (called from the `.ino`, right before `setCallZone()`) — NOT just once at boot. This is what makes Color Row / Brightness / Active LED Count / Default LED On / Idle Blink Interval take effect immediately after saving Form 3, without a reboot. Most other config (route, button variant, PCB revision, ruleset) is still boot-time-only and needs Form 0 → Reboot to pick up a change.

**Disconnect blink (`setLinkStatus()`, matches the reference firmware's `setLedStatus()` priority order exactly)**: every loop iteration, the `.ino` calls `g_ledController->setLinkStatus(g_statusUplink->isNetworkUp(), g_statusUplink->isLinkUp())`. In `repaint()`, this is checked **before** anything else and, if either is false, completely overrides the call-state color for the whole zone (the door indicator's full strip, or just `leds_[0]` for bed/toilet/combo — same zone that would otherwise show the call color):
- `networkUp == false` (no WiFi STA association / no Ethernet link at all) → blinks `DISCONNECT_WHITE` ↔ `CLEAR` (black). Most severe, wins if both are false.
- `networkUp == true` but `serverUp == false` (WiFi's fine, but the configured WebSocket/Socket.IO/RS485-poll/Ethernet-handshake isn't confirmed) → blinks `DISCONNECT_PINK` ↔ `CLEAR`.
- Both true → normal call-state color as described above.

This uses its own independent `BlinkController` (`linkBlink_`, 500ms period) so it doesn't interact with the housekeeping blink's state.

**Pixel-level debug (`logPixels()`)**: every `repaint()` call ends with a `debugdata()` trace listing the actual `#RRGGBB` value written to every active pixel (plus `colorRow_`, `activeCount_`, and current `FastLED.getBrightness()`), tagged e.g. `LED PIXELS [call] row=0 count=8 bright=80 : [0]#FF0000 [1]#000000 ...`. This is the ground truth of what the firmware computed and sent to the strip — visible live in the `/forms` page's debug log (see §4). If the trace shows the expected color (e.g. `#FF0000` for a CALL) but the physical strip doesn't light up that way, the bug is downstream of software (wiring, `colorRow`/GRB-BRG mismatch, power, or a bad pixel) rather than in the call-state → color logic.

**GAP**: the LED data pin is hardcoded to GPIO0 (`#define LED_DATA_PIN 0` in `LedStripController.h`) for every role — FastLED's `addLeds<CHIPSET, PIN, ...>()` binds the pin at compile time, which can't be made runtime-configurable without a different FastLED API. One deprecated door-indicator PCB revision used GPIO13 and is not supported by this unified image.

## 9. Consolidated list of known gaps (searchable)

- Form 13 field "Ruleset Preset" is stored but never read — no behavioral effect.
- Form 13's `CustomState`/`CallState::CUSTOM` has no trigger wired to it anywhere — enabling it has no visible effect until something sets `mainState = CallState::CUSTOM`.
- Form 2 "Allow Reboot" is stored but never checked before a reboot occurs.
- Form 4 "New 485 Module" and "Legacy Protocol Select" are stored but unused by the current hardware-UART-based `TransportRs485` (they mattered for the legacy SoftwareSerial implementation this replaced).
- Form 12 slots 2–6 (Toilet/Extra/Blue/Attend/AP) have no effect on Gpio2/Gpio3Remote button variants — only slots 0/1 are read by those drivers, with no UI indication that the other 5 dropdowns are inert for the selected variant.
- OTA firmware update is a stub (`handleBinUpdate` just logs and returns) — explicitly out of scope, flagged as a valuable fast-follow since one image now covers every variant.
- LED data pin is fixed to GPIO0 for all roles/PCB revisions.
- When `toiletid` names a distinct device, this bed unit's pull-cord press sends one fire-and-forget `Call(1)` under that id (§6/§8) — there is no corresponding "clear"/cancel event wired for that case (unlike the shared-unit case, where `CANCEL` on the IDLE+toiletCallActive ladder clears it). If a given site's pull-cord hardware produces a distinct release/reset signal, it isn't currently routed to send a clearing `0` status for the separate-toiletid case — scoped out pending a concrete hardware spec for that signal.

**Operational note — EEPROM `CONFIG_STRUCT_VERSION`**: `NurseCallConfig.cpp` bumps this any time `DeviceConfig`'s layout changes, even append-only, because `configLoad()`'s version mismatch is the *only* thing that resets a unit to sane defaults. A field appended after a device's last flash-at-that-version keeps whatever garbage was already at that EEPROM offset — this can look exactly like a logic bug (wrong LED strip length, inert ruleset thresholds) when it's actually stale EEPROM. If a setting behaves inconsistently after adding a new `DeviceConfig` field, bump this constant before debugging further — every already-flashed unit will fall back to defaults once and need its settings redone.
