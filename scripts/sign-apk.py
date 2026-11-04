#!/usr/bin/env python3
"""Signs an APK (signature scheme v2) without needing Java. The key lives outside the repository,
in ~/.android/raptorsprint-key.pem, and is made the first time. Keep it: an app can only be updated
by one signed with the same key.
Usage: sign-apk.py in.apk out.apk"""
import datetime
import hashlib
import os
import struct
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.x509.oid import NameOID

KEY = os.path.expanduser("~/.android/raptorsprint-key.pem")
RSA_PKCS1_SHA256 = 0x0103
V2_BLOCK_ID = 0x7109871A


def load_key():
    if os.path.exists(KEY):
        data = open(KEY, "rb").read()
        key = serialization.load_pem_private_key(data, None)
        return key, x509.load_pem_x509_certificate(data)
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Raptor Sprint")])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now)
            .not_valid_after(now + datetime.timedelta(days=36500)).sign(key, hashes.SHA256()))
    os.makedirs(os.path.dirname(KEY), exist_ok=True)
    with open(os.open(KEY, os.O_CREAT | os.O_WRONLY, 0o600), "wb") as f:
        f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        f.write(cert.public_bytes(serialization.Encoding.PEM))
    return key, cert


def lp(data):  # length-prefixed
    return struct.pack("<I", len(data)) + data


def digest(sections):
    chunks = []
    for s in sections:
        for i in range(0, len(s), 1 << 20):
            c = s[i:i + (1 << 20)]
            chunks.append(hashlib.sha256(b"\xa5" + struct.pack("<I", len(c)) + c).digest())
    return hashlib.sha256(b"\x5a" + struct.pack("<I", len(chunks)) + b"".join(chunks)).digest()


def main(src, dst):
    apk = open(src, "rb").read()
    eocd = apk.rfind(b"PK\x05\x06")
    cd = struct.unpack("<I", apk[eocd + 16:eocd + 20])[0]
    entries, central, end = apk[:cd], apk[cd:eocd], apk[eocd:]
    key, cert = load_key()
    # (The end record is hashed as it will read once the block has gone in: it's hashed with the
    # central directory's offset as if the block weren't there.)
    d = digest([entries, central, end])
    signed = lp(lp(struct.pack("<I", RSA_PKCS1_SHA256) + lp(d))) + lp(lp(cert.public_bytes(serialization.Encoding.DER))) + lp(b"")
    sig = key.sign(signed, padding.PKCS1v15(), hashes.SHA256())
    public = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    signer = lp(signed) + lp(lp(struct.pack("<I", RSA_PKCS1_SHA256) + lp(sig))) + lp(public)
    value = lp(lp(signer))
    pair = struct.pack("<QI", len(value) + 4, V2_BLOCK_ID) + value
    size = len(pair) + 8 + 16
    block = struct.pack("<Q", size) + pair + struct.pack("<Q", size) + b"APK Sig Block 42"
    end = end[:16] + struct.pack("<I", cd + len(block)) + end[20:]
    open(dst, "wb").write(entries + block + central + end)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
