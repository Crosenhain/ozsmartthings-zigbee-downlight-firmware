"""Build Zigbee2MQTT OTA request payloads for a local .zigbee file.

Writes, next to the OTA file:
  <name>.update_hex.json   -> publish to zigbee2mqtt/bridge/request/device/ota_update/update
                              (file embedded as hex; Z2M writes it to its data dir itself)
  <name>.update_path.json  -> same topic, for when the file has been copied onto the Z2M host
                              (url = absolute path on that host)

usage: python make_ota_payload.py OTAFILE DEVICE [--z2m-path /app/data/ota/file.zigbee] [--base-topic zigbee2mqtt]
DEVICE is the Z2M friendly name or IEEE address.
"""
import argparse
import json
import os


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("otafile")
    ap.add_argument("device")
    ap.add_argument("--z2m-path", help="absolute path of the copied file on the Z2M host")
    ap.add_argument("--base-topic", default="zigbee2mqtt")
    args = ap.parse_args()

    data = open(args.otafile, "rb").read()
    stem = os.path.splitext(args.otafile)[0]
    name = os.path.basename(args.otafile)
    topic = f"{args.base_topic}/bridge/request/device/ota_update/update"

    hex_payload = {"id": args.device, "hex": {"data": data.hex(), "file_name": name}}
    with open(stem + ".update_hex.json", "w") as fh:
        json.dump(hex_payload, fh, separators=(",", ":"))
    print(f"topic:   {topic}")
    print(f"hex:     {stem}.update_hex.json ({os.path.getsize(stem + '.update_hex.json'):,} bytes)")

    if args.z2m_path:
        with open(stem + ".update_path.json", "w") as fh:
            json.dump({"id": args.device, "url": args.z2m_path}, fh)
        print(f"path:    {stem}.update_path.json -> {args.z2m_path}")


if __name__ == "__main__":
    main()
