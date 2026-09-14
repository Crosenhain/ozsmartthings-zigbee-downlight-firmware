"""Build a Zigbee2MQTT/zigbee-herdsman OTA index for a local .zigbee file, plus a check payload.

Relative `url`s are resolved against Z2M's data directory, so copy the index and the
.zigbee file into `<z2m data dir>/ota/` and publish the payload to
  zigbee2mqtt/bridge/request/device/ota_update/check

usage: python make_ota_index.py OTAFILE DEVICE [--rel-dir ota]
"""
import argparse
import hashlib
import json
import os
import struct


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("otafile")
    ap.add_argument("device")
    ap.add_argument("--rel-dir", default="ota")
    args = ap.parse_args()

    data = open(args.otafile, "rb").read()
    mfr, image_type, version = struct.unpack_from("<HHI", data, 10)
    header_string = data[20:52].rstrip(b"\x00").decode(errors="replace")
    name = os.path.basename(args.otafile)
    stem = os.path.splitext(args.otafile)[0]

    entry = {
        "fileName": name,
        "fileVersion": version,
        "fileSize": len(data),
        "url": f"{args.rel_dir}/{name}",
        "imageType": image_type,
        "manufacturerCode": mfr,
        "sha512": hashlib.sha512(data).hexdigest(),
        "otaHeaderString": header_string,
    }
    index_path = stem + ".index.json"
    with open(index_path, "w") as fh:
        json.dump([entry], fh, indent=2)
    payload_path = stem + ".check.json"
    with open(payload_path, "w") as fh:
        json.dump({"id": args.device, "url": f"{args.rel_dir}/{os.path.basename(index_path)}"}, fh)

    print(f"index:   {index_path}  (mfr 0x{mfr:04x}, type 0x{image_type:04x}, version {version} / 0x{version:08x})")
    print(f"payload: {payload_path}")
    print(open(payload_path).read())


if __name__ == "__main__":
    main()
