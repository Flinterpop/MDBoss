"""Regenerate ``app/LogoAsset.h`` from ``background-logo.png``.

The tech-note template carries the banner logo as a ``data:`` URI so a
brand-new document renders it before it has a folder for a relative
``<img src>`` to resolve against.  The app decodes the same blob back to bytes
when it writes the ``.png`` beside a saved document, so the header is the one
source of truth for the artwork and this script is the only thing that should
write it.

Run from anywhere::

    python MDBossCpp/tools/embed_logo.py

It is deliberately not wired into the build: the logo changes about never, and
a generated file that is regenerated on every build is a merge conflict
waiting to happen.
"""

from __future__ import annotations

import base64
import hashlib
import pathlib
import sys

# Wrap the base64 so the header stays readable in a diff and no line runs past
# the 79-column convention the rest of the C++ keeps to.
CHUNK = 72

HEADER = '''\
// The tech-note banner logo, embedded so a new document renders it before it
// has a folder to resolve a relative <img src> against.
//
// GENERATED FILE -- do not edit by hand.  Produced from background-logo.png
// ({size} bytes, SHA-256 {digest}) by MDBossCpp/tools/embed_logo.py.
//
// This is the single source of truth for the logo: Templates.cpp decodes it
// both to build the data: URI the template carries and to write the .png file
// beside a saved document.

#ifndef MDBOSS_APP_LOGO_ASSET_H
#define MDBOSS_APP_LOGO_ASSET_H

#include <cstddef>

namespace mdboss {{

// Decoded size, so the base64 round trip can be checked rather than trusted.
inline constexpr std::size_t kLogoPngBytes = {size};

// Base64 of background-logo.png, one string once the adjacent literals are
// concatenated.  ASCII only, so the narrow-literal guard in test_sources.cpp
// is satisfied.
inline constexpr char kLogoPngBase64[] =
'''

FOOTER = '''
}}  // namespace mdboss

#endif  // MDBOSS_APP_LOGO_ASSET_H
'''


def render(png: bytes) -> str:
    """Return the full text of LogoAsset.h for ``png``."""
    assert png, "the logo must not be empty"
    assert png[:8] == b"\x89PNG\r\n\x1a\n", "the logo must be a PNG"

    encoded = base64.b64encode(png).decode("ascii")
    assert base64.b64decode(encoded) == png, "base64 round trip must be exact"

    lines = [encoded[i:i + CHUNK] for i in range(0, len(encoded), CHUNK)]
    assert lines, "an empty encoding would emit an invalid literal"

    body = ""
    for index, line in enumerate(lines):  # bounded by the encoded length
        tail = ";" if index == len(lines) - 1 else ""
        body += f'    "{line}"{tail}\n'

    text = HEADER.format(
        size=len(png), digest=hashlib.sha256(png).hexdigest()
    ) + body + FOOTER.format()
    assert "kLogoPngBase64" in text, "the header must define the blob"
    return text


def main() -> int:
    """Rewrite the header; return a process exit code."""
    here = pathlib.Path(__file__).resolve()
    repo = here.parent.parent.parent           # tools -> MDBossCpp -> repo
    source = repo / "background-logo.png"
    target = here.parent.parent / "app" / "LogoAsset.h"

    if not source.is_file():
        sys.stderr.write(f"missing artwork: {source}\n")
        return 1

    png = source.read_bytes()
    text = render(png)
    target.write_text(text, encoding="utf-8", newline="\n")
    sys.stdout.buffer.write(
        f"wrote {target} ({len(png)} bytes -> {len(text)} chars)\n".encode()
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
