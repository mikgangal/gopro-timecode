"""Enable the GoPro Wi-Fi access point and report credentials.

This follows the Wi-Fi preparation guidance from the Open GoPro BLE setup doc:
https://gopro.github.io/OpenGoPro/ble/protocol/ble_setup.html
"""

from __future__ import annotations

import argparse
import asyncio
import logging

from open_gopro import WirelessGoPro


def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Use BLE to turn on the camera's Wi-Fi AP (GPxxxx) and show the SSID/password "
            "so the host PC can hop onto that network for HTTP control."
        ),
    )
    parser.add_argument(
        "--serial",
        dest="serial_suffix",
        help="Last 4 digits of the camera serial (default: auto-discover the first camera in pairing mode).",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=15,
        help="Seconds to wait per BLE attempt (default: 15).",
    )
    parser.add_argument(
        "--retries",
        type=int,
        default=5,
        help="How many BLE scan/connect retries before failing (default: 5).",
    )
    parser.add_argument(
        "--ready-polls",
        type=int,
        default=25,
        help="Max polls for the AP mode status bit (default: 25).",
    )
    parser.add_argument(
        "--log-level",
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        help="Verbosity for this helper (default: INFO).",
    )
    return parser.parse_args()


async def wait_for_ap_mode(gopro: WirelessGoPro, attempts: int) -> None:
    """Wait for the AP mode status flag to turn on."""
    for attempt in range(1, attempts + 1):
        ap_status = await gopro.ble_status.ap_mode.get_value()
        if ap_status.data:
            logging.info("AP Mode is enabled (status poll #%s).", attempt)
            return
        await asyncio.sleep(0.2)
    raise TimeoutError("Timed out waiting for the camera to report AP Mode readiness.")


async def main() -> None:
    args = _parse_args()
    logging.basicConfig(level=getattr(logging, args.log_level), format="%(asctime)s %(levelname)s %(message)s")

    gopro = WirelessGoPro(
        target=args.serial_suffix,
        interfaces={WirelessGoPro.Interface.BLE},
    )

    try:
        await gopro.open(timeout=args.timeout, retries=args.retries)
        ssid = (await gopro.ble_command.get_wifi_ssid()).data
        password = (await gopro.ble_command.get_wifi_password()).data
        logging.info("Current camera Wi-Fi SSID: %s", ssid)
        logging.info("Current camera Wi-Fi password: %s", password)

        logging.info("Enabling the camera Wi-Fi access point via BLE...")
        await gopro.ble_command.enable_wifi_ap(enable=True)
        await wait_for_ap_mode(gopro, args.ready_polls)
        logging.info(
            "Wi-Fi AP is broadcasting. Connect your PC Wi-Fi interface to SSID '%s' with password '%s' to send HTTP commands.",
            ssid,
            password,
        )
        logging.info("The camera will keep the AP on until you disable it or the camera powers down.")
    finally:
        await gopro.close()
        logging.info("BLE session closed.")


if __name__ == "__main__":
    asyncio.run(main())


