#!/usr/bin/env python3
"""
Запрос /v1/services с AGW-шифрованием.
Печатает все services/connections с service_protocol, connection_uuid, node_id, env, label.
Usage: python3 fetch_frkn_services.py <subscription_id>
"""

import json
import sys

from fetch_frkn_config import load_public_key, encrypt_request, decrypt_response, AGW_PUBLIC_KEY_PATH, GATEWAY_ENDPOINT
import os
import urllib.request


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 fetch_frkn_services.py <subscription_id>")
        sys.exit(1)

    subscription_id = sys.argv[1]
    public_key = load_public_key(AGW_PUBLIC_KEY_PATH)

    api_payload = {
        "os_version": "ios",
        "app_language": "ru",
        "auth_data": {"id": subscription_id, "api_key": subscription_id},
    }

    body, aes_key, aes_iv = encrypt_request(public_key, api_payload)
    req = urllib.request.Request(
        url=f"{GATEWAY_ENDPOINT}/v1/services",
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    with urllib.request.urlopen(req, timeout=30) as resp:
        decrypted = decrypt_response(aes_key, aes_iv, resp.read())

    for svc in decrypted.get("services", []):
        print(f"service: {svc.get('service_type')}/{svc.get('service_protocol')} available={svc.get('is_available')}")
        for conn in svc.get("connections", []):
            print(f"  conn: proto={conn.get('service_protocol')!r} uuid={conn.get('connection_uuid')} "
                  f"node_id={conn.get('node_id')} env={conn.get('env')!r} country={conn.get('country_code')} "
                  f"label={conn.get('connection_label')!r}")
        if not svc.get("connections"):
            print("  (no connections)")


if __name__ == "__main__":
    main()
