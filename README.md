# gopro-timecode

Utilities for driving a GoPro HERO12 (Labs build) from Windows using the
Open GoPro BLE protocol.[^ble-setup]

[^ble-setup]: https://gopro.github.io/OpenGoPro/ble/protocol/ble_setup.html

## Prerequisites

- Put the camera into BLE pairing mode from the Labs utility so that it
  advertises service `0xFEA6` as described in the BLE setup guide.[^ble-setup]
- Install Python 3.12 (the Python Launcher is already on the box, so `winget install -e --id Python.Python.3.12` works).
- Create/activate the project virtual environment and install dependencies:

```powershell
cd C:\github\gopro-timecode
py -3.12 -m venv .venv
.\.venv\Scripts\activate
python -m pip install -r requirements.txt
```

## Bring up the BLE link

The script in `scripts/ble_connect.py` drives steps 2–7 from the BLE setup
playbook: scanning for the camera, opening the BLE link, discovering GATT
services, enabling command/response characteristics, and repeatedly issuing
`GetHardwareInfo` until the camera reports `status=0 (SUCCESS)`, which is the
“ready for more commands” signal called out in the spec.[^ble-setup]

```powershell
# Use the last four digits of the camera serial number to narrow discovery.
python scripts/ble_connect.py --serial 1234 --log-level DEBUG
```

The log shows:

1. BLE scanning/connection attempts (`--timeout` and `--retries` tune this).
2. Polling of `GetHardwareInfo` (`--ready-attempts` / `--ready-delay`), which
   mirrors the “Wait for Camera BLE Readiness” loop from the spec.
3. A pretty-printed `GoProResp` once the status changes to `SUCCESS`.

When finished, the script closes the BLE session so the camera can move on to
Wi-Fi provisioning. Leave the shell open—we will reuse the same virtual
environment when wiring up Wi-Fi AP control next.

## Turn on the camera Wi-Fi AP

Once BLE is confirmed, run `scripts/wifi_ap_enable.py` to flip on the camera’s
`GPxxxx` access point and print the SSID/password pulled via BLE:

```powershell
python scripts/wifi_ap_enable.py --serial 1234
```

The helper waits for the `StatusId.AP_MODE` flag (step 7 from the spec) so you
know the radio is broadcasting before you switch your PC’s Wi-Fi interface over
to that SSID. The AP keeps running until you turn it back off (or the camera
powers down), so it remains ready for HTTP control at
`http://10.5.5.9:8080/` even after the BLE session ends.[^ble-setup]