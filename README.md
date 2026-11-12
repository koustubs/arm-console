# Arm Console

A local dashboard and Arduino Uno sketch for a pick-and-place arm. The original source is preserved in `baseline/Robotic_Arm_Controller`. No hardware has been connected or tested during development.

The app can compile and upload either sketch using the normal Uno bootloader. Both are ordinary Arduino sketches: uploading one replaces the sketch currently on the board, and you can switch back by uploading the saved original sketch again. Arduino IDE's normal **Upload** button is another option. Neither method needs **Burn Bootloader** or **Upload Using Programmer**.

## Quick start without hardware

1. Install Python 3 if it is not already available. Double-click `launch.bat`; keep the terminal window open. Open **http://localhost:49152** in a current desktop Chrome or Edge if it does not open there automatically.
2. Select **Try demo**. The display and exported rows say `demo`; these are generated values, not experimental results. Use **Disconnect** to end the demo.
3. **Export CSV** saves the records currently held in the browser. There is no server-side persistence; export before closing or refreshing the page. The last 20,000 lines are retained; older lines are dropped. **Clear log** erases the current session.

To run manually: `python launcher.py`. Use this launcher rather than a generic static server: its local service handles firmware uploads. There is no cloud account. The browser's port chooser is a security requirement. **Connect Arduino** lets you select a device without typing a COM number. **Reconnect permitted device** works when exactly one device has already been permitted to this browser and this localhost origin. If there are multiple, use the chooser. Close Arduino IDE's Serial Monitor and other serial applications before connecting or uploading.

## Upload from the app

1. Install [Arduino CLI](https://docs.arduino.cc/arduino-cli/installation/) and ensure `arduino-cli` is on your PATH, then restart the launcher. In the Firmware panel, click **Install build tools** once to download the AVR board package and Servo library into this project. This step requires internet access.
2. Plug in an Arduino Uno, click **Find ports**, and select its port. A port marked unidentified might belong to another device; confirm it is the Uno. Select **Arm Console firmware** or **Original sketch** and click **Upload sketch**. The app asks before replacing the current sketch, disconnects its own serial session, compiles, uploads and requests verification. It never uploads merely on connection.
3. After upload, choose **Connect Arduino** for live data. Uploading and connecting can reset the board. The original sketch starts its sequence automatically after reset. If an upload fails or is interrupted, the board's sketch state is uncertain; check the error and retry or use Arduino IDE.

The launcher binds to loopback and accepts only its predefined setup and upload operations. Keep the browser and launcher on the same computer. The upload process is software-tested without a board and still needs a supervised physical test.

Opening a serial connection can reset a classic Uno. If the **original** sketch is loaded, it automatically starts moving after reset. The new sketch remains idle until Start, but its first Start attaches servos and commands the original startup angles. Connect only when the arm is clear and supervised.

## Using the original sketch

Upload `baseline/Robotic_Arm_Controller/Robotic_Arm_Controller.ino` using the usual Arduino IDE steps, or keep the sketch already on your board. Connect at 9600 baud. The app interprets `DISTANCE` and `GRIP` and records other text as events. Start and Stop are disabled because the original sketch has no command interface. Angles, device timestamps, states, echo time and precise cycle boundaries are not available from it. The two original detection messages are not counted as two cycles.

## Using the new sketch

1. Upload **Arm Console firmware** from the app as described above, or open `firmware/ArmController/ArmController.ino` in Arduino IDE, install the standard **Servo** library if needed, select the correct board and port, and use the normal **Upload** button. This is an ordinary Arduino sketch, not a replacement bootloader. Keep your original sketch as a backup. The sketch is designed for the original Uno pinout and 9600 baud.
2. Close Serial Monitor and connect in the web app. The firmware reports its identity and sends samples even while idle. Start/Stop become available only when its protocol is detected. Start runs **one** search and transfer sequence; after COMPLETE, press Start for another. It does not automatically resume after a reset or reconnect.
3. Before the first movement, verify wiring and power, make sure the actual joint limits and direction match the sketch, clear the workspace, and have a way to disconnect motor power. Use a supervised, unloaded test and be ready to remove power. Startup servo angles are 80/180/0/30 degrees (base/shoulder/elbow/gripper); on first Start those initial commands can cause an immediate movement. Physical pose cannot be inferred from commands. Do not assume servo power arrangements or switch installation.
4. **Stop is a software request, not an emergency stop**: it stops issuing further movement steps and continues holding the last commands; communication can fail or lag. The sketch can block on an ultrasonic echo for up to 30 ms plus serial overhead. Disconnecting the browser does not stop the arm. Use a physical motor-power disconnect when needed. Reset or power loss can change servo behavior. Restarting after a stop or fault moves from the last software-commanded angles toward the scan pose and may move an object; inspect and clear the mechanism first.

The pin mapping is D3 base, D5 shoulder, D6 elbow, D9 gripper, D12 trigger, D11 echo, D2 contact input. D2 uses `INPUT_PULLUP` and is reported as active when LOW; an inactive value does not establish that a switch is installed. It is telemetry only. Motion is not conditioned on it. No grip force, actual joint position, object identity, or physical grasp success is measured. `COMPLETE` only means the commanded sequence finished.

Relative to the baseline, the new sketch starts only on request, uses a shared sampled range for decisions and telemetry, sweeps from the current base command, stops in FAULT after 10 consecutive absent or below-2-cm readings during approach, an approach limit, or 15 seconds, and never intentionally closes following those faults. The 2–4 cm pickup range and original named target angles remain starting values, not verified calibration. Sampling is nominally 150 ms, not hard real-time. Behavior and timing differ from the original, and the mechanism has not been tested. The software does not perform object classification or verify a successful transfer.

## Data and protocol

Firmware transmits newline-delimited ASCII at 9600 baud:

- `ARM,1` identifies the protocol; `HELLO`, `START`, and `STOP` are newline-terminated host commands. `A,<command>` acknowledges, `N,<reason>` rejects, `E,<millis>,<state>` changes state, and `F,<reason>` describes a fault. An acknowledgement is not proof of physical movement.
- `S,sequence,millis,echo_us,distance_cm,grip,state,base,shoulder,elbow,gripper` is a sample. A zero echo and `-1.0` represent a timeout, not a negative distance. Firmware time is milliseconds since startup and wraps; resetting restarts sequence/time. Angles are commanded targets; before the first Start they are startup defaults that have not been sent to servos.
- The dashboard retains the raw serial line and host receipt timestamp. Its filtered distance is an arithmetic mean of the latest up to five valid consecutive readings; an invalid echo clears the window and leaves a gap. Filtering is display-only: the firmware makes decisions on the raw measurement. A 3-second stale warning is based on browser receipt time. CSV fields are blank where the original protocol does not provide a value.

## Development checks

`node tests/core.test.js` tests parsing, split serial lines, invalid samples, filtering and CSV quoting. `node tests/app.test.js` exercises demo mode, a simulated serial connection and the upload UI. `python -m unittest discover -s tests -p "test_*.py"` checks the upload sequence, failure paths and local request protections without a board. With Arduino CLI, an installed AVR board core and Servo library: `arduino-cli compile --fqbn arduino:avr:uno firmware/ArmController`. Both sketches compiled for Uno during development with AVR core 1.8.8 and Servo 1.3.0. Hardware behavior, the serial port chooser on the target machine, and physical calibration remain to be verified.

## License

[MIT](LICENSE).
