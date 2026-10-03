import argparse
import json
from pathlib import Path
import socket
import urllib.error
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", required=True)
    parser.add_argument("--token-file", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    token = Path(args.token_file).read_text().strip()
    results = []

    def request(path, expected=200, body=None, authenticate=True):
        headers = {"Authorization": "Bearer " + token} if authenticate else {}
        try:
            response = urllib.request.urlopen(urllib.request.Request(args.url + path, data=body, headers=headers), timeout=20)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            payload = response.read()
            assert response.status == expected, (path, response.status, expected)
        results.append({"path": path, "expected": expected, "passed": True})
        return payload

    assert json.loads(request("/health", authenticate=False))["protocol"] == 2
    request("/models", 401, authenticate=False)
    model = json.loads(request("/models"))[0]["id"]
    request("/unknown", 404)
    base = f"NGSREQ2 123 {model} 64 64 0 0 1 rgba".encode()
    for body in (b"NGSREQ1 64 64 0 rgba", base + b" trailing", base.replace(b"rgba", b"f32"), base.replace(b"64 64", b"4097 64"), base.replace(b"0 0 1", b"0 90 1"), base.replace(model.encode(), b"../escape"), base.replace(b"123", b"-1")):
        request("/frame", 400, body)
    for quality in ("rgba", "jpeg95", "jpeg90", "jpeg85"):
        payload = request("/frame", body=base.replace(b"rgba", quality.encode()))
        assert payload[:8] == b"NGSFRM02"
    address = urllib.parse.urlsplit(args.url)
    if address.scheme == "http":
        with socket.create_connection((address.hostname, address.port), timeout=3) as connection:
            connection.settimeout(15)
            connection.sendall(b"GET /health HTTP/1.1\r\nHost: localhost\r\nHost: duplicate\r\n\r\n")
            assert b"400" in connection.recv(1024).split(b"\r\n")[0]
        results.append({"path": "duplicate-header", "passed": True})
        with socket.create_connection((address.hostname, address.port), timeout=3) as connection:
            connection.settimeout(15)
            connection.sendall(b"GET /health HTTP/1.1\r\nHost: partial")
            assert b"504" in connection.recv(1024).split(b"\r\n")[0]
        results.append({"path": "partial-header-total-deadline", "passed": True})
    request("/health", authenticate=False)
    Path(args.output).write_text(json.dumps(results, indent=2))
    print(str(len(results)) + " integration checks passed")


if __name__ == "__main__":
    main()
