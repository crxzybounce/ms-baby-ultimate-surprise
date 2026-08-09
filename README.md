# Ultimate Surprise for Ms Baby — Fresh Start

This repository is the clean restart of the project.

## What this version uses

- Adafruit Feather nRF52840 Express
- MAX30102 heart-rate sensor
- MCP9808 temperature/contact sensor
- MPU6050 accelerometer/gyro
- Phone as the only game display
- USB power
- Bluetooth Low Energy using Nordic UART Service
- No LCD
- No external LEDs
- No battery code

## Wiring

All three sensors share the same I2C bus.

| Feather | MAX30102 | MCP9808 | MPU6050 |
|---|---|---|---|
| 3V | VCC/VIN | VIN/VCC | VCC |
| GND | GND | GND | GND |
| SDA | SDA | SDA | SDA |
| SCL | SCL | SCL | SCL |

Leave MAX30102 INT, MCP9808 ALERT, and MPU6050 INT/XDA/XCL disconnected.

## Arduino setup

1. In Arduino IDE, install/update **Adafruit nRF52 by Adafruit** from Boards Manager.
2. Select:
   **Tools → Board → Adafruit nRF52 Boards → Adafruit Bluefruit nRF52840 Feather Express**
3. Install:
   **SparkFun MAX3010x Pulse and Proximity Sensor Library**
4. If compilation says it is using `DevLab_MAX30102` for `MAX30105.h`, remove that DevLab library. The final firmware is written for the SparkFun library.
5. Open:
   `firmware/UltimateSurprise_FreshStart.ino`
6. Upload it.
7. Open Serial Monitor at **115200**, then press RESET once.

A healthy startup should look similar to:

```text
ULTIMATE SURPRISE FOR MS BABY - FRESH START v1.0
Starting sensor initialization...
I2C lines released: OK
MCP9808: OK
MAX30102: OK
MPU6050: OK at 0x68
Sensor initialization finished.
Starting Bluetooth...
BLE advertising started: ULTIMATE-SURPRISE

READY
BLE name: ULTIMATE-SURPRISE
MAX30102: OK
MCP9808:  OK
MPU6050:  OK
```

Do not move on to the phone website until `READY` appears.

## Make a completely new GitHub repository

Recommended repository name:

`ms-baby-ultimate-surprise`

1. On GitHub choose **New repository**.
2. Name it exactly `ms-baby-ultimate-surprise`.
3. Make it **Public** for the simplest GitHub Pages setup.
4. Do not start from your old repository.
5. Upload the contents of this ZIP/repository folder so that `index.html` is at the top/root level.

The important structure is:

```text
ms-baby-ultimate-surprise/
├── index.html
├── 404.html
├── .nojekyll
├── README.md
└── firmware/
    └── UltimateSurprise_FreshStart.ino
```

Then open:

**Settings → Pages → Build and deployment**

Choose:

- Source: `Deploy from a branch`
- Branch: `main`
- Folder: `/(root)`

Save.

GitHub will show the exact published URL. For a normal project repository it will usually be:

`https://YOUR-GITHUB-USERNAME.github.io/ms-baby-ultimate-surprise/`

Use the URL GitHub itself shows in Settings → Pages rather than typing an old URL.

## Phone browser

The website must be opened from the HTTPS GitHub Pages address.

- iPhone/iPad: open the GitHub Pages URL inside a Web Bluetooth browser such as **Bluefy**. Safari does not expose Web Bluetooth.
- Android: use Chrome.

Do not pair `ULTIMATE-SURPRISE` from the normal phone Bluetooth Settings screen. Press **Click to Play & Connect** on the website and select `ULTIMATE-SURPRISE` in the browser's Bluetooth chooser.

## First Bluetooth test

After connecting, open the **Bluetooth debug** section at the bottom of the page.

You should see a sequence similar to:

```text
> PING
< COMMAND_ACK,PING
< PONG
> STATUS
< COMMAND_ACK,STATUS
< STATUS,1,1,1
```

When Task 1 begins:

```text
> START_BASELINE
< COMMAND_ACK,START_BASELINE
< SCAN_STARTED,BASELINE
< SCAN_PROGRESS,BASELINE,...
```

If `STATUS` is not `1,1,1`, fix that sensor before testing the game flow.

## Game flow

1. 2023 → 2024 → 2025 → 2026 → 2027 intro.
2. `Ultimate Surprise for Ms Baby 🔥🔥`
3. 30-second baseline.
   - Live BPM.
   - Live temperature.
   - Countdown.
   - Next button only after the full 30 seconds.
4. 10 pushups.
5. 30-second post-pushup scan.
6. 50 mountain climbers.
7. 30-second post-mountain-climber scan.
8. Game target:
   - HR increase at least 10% from baseline.
   - Temperature increase at least 5% from baseline.
   - Maximum one mountain-climber retry.
9. Walking lunges using a real 5 m floor marker:
   - 5 m out.
   - Turn around.
   - 5 m back.
   - MPU6050 verifies motion and turnaround; it does not claim to measure exact distance.
10. Surprise reveal.
11. Accept → `🥳🎉💖 CANCUN 2027!!!`
12. Decline → `😢💩`
13. Restart option appears after 3 seconds.

## Sensor note

This is a game, not a medical device. The MCP9808 is being used as a contact-temperature trend sensor, not as a clinical skin-temperature measurement.
