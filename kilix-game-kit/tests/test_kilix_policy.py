"""tools/kilix_policy.py against the C loader's reference blob and failure modes."""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import kilix_policy as kp  # noqa: E402

PARAMS = [1, -1, 0.5, 0.5, -2, 1, 0, 0.25, -0.5, 1, 2, -1, -1, 0.5, 3, 0.1, -0.2]


def expect_error(fn, *args):
    try:
        fn(*args)
    except ValueError:
        return
    raise AssertionError(f"{fn.__name__}{args!r} did not raise")


def main():
    raw = struct.pack("<17f", *PARAMS)
    blob = kp.pack([2, 3, 2], raw, 1.5)
    info = kp.inspect(blob)
    # Same digest the C test asserts for its independently written blob.
    assert info["fnv1a64"] == "c08fcc42e5238118", info
    assert info["bytes"] == 108 and info["parameters"] == 17 and info["temperature"] == 1.5

    expect_error(kp.pack, [2, 3, 2], raw[:-4], 1.0)
    expect_error(kp.pack, [2, 0, 2], raw, 1.0)
    expect_error(kp.pack, [2, 3, 2], raw, 0.0)
    expect_error(kp.pack, [2, 3, 2], struct.pack("<17f", float("nan"), *PARAMS[1:]), 1.0)
    expect_error(kp.inspect, blob[:-1])
    expect_error(kp.inspect, blob + b"\0")
    expect_error(kp.inspect, b"KXPOLICZ" + blob[8:])
    damaged = bytearray(blob)
    damaged[40] ^= 1
    expect_error(kp.inspect, bytes(damaged))

    with tempfile.TemporaryDirectory() as tmp:
        raw_path, blob_path, header = (os.path.join(tmp, n) for n in ("p.f32", "p.kxpol", "p.h"))
        with open(raw_path, "wb") as fh:
            fh.write(raw)
        tool = [sys.executable, os.path.join(HERE, "..", "tools", "kilix_policy.py")]
        subprocess.run(tool + ["pack", "--widths", "2", "3", "2", "--temperature", "1.5",
                               raw_path, blob_path], check=True, stdout=subprocess.DEVNULL)
        with open(blob_path, "rb") as fh:
            assert fh.read() == blob
        subprocess.run(tool + ["embed", blob_path, "--symbol", "test_policy", header], check=True)
        with open(header, encoding="utf-8") as fh:
            text = fh.read()
        assert "TEST_POLICY_FNV1A64 UINT64_C(0xc08fcc42e5238118)" in text
        assert "static const uint8_t test_policy[108]" in text
        ok = subprocess.run(tool + ["embed", blob_path, "--symbol", "test_policy", header, "--check"],
                            stdout=subprocess.DEVNULL)
        assert ok.returncode == 0
        with open(header, "a", encoding="utf-8") as fh:
            fh.write("/* drift */\n")
        stale = subprocess.run(tool + ["embed", blob_path, "--symbol", "test_policy", header, "--check"],
                               stderr=subprocess.DEVNULL)
        assert stale.returncode == 1
        bad = subprocess.run(tool + ["embed", blob_path, "--symbol", "9bad", header],
                             stderr=subprocess.DEVNULL)
        assert bad.returncode == 1
    print("ok: kilix_policy.py")


if __name__ == "__main__":
    main()
