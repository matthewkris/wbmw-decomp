#!/bin/sh
# Usage: tools/makepack.sh [ROM]       (run by `make -C native`; from anywhere)
# Makes assets/game.pak, the game's data that wbmw plays from (docs/EXTRACT.md), and the music as
# MIDI files (assets/sound/midi, tools/midi.py): extracts the
# ROM into assets/ when assets/ is missing or was written by another version of the extractor,
# then packs assets/ when the pack is missing or older than what it is made from. assets/ is
# otherwise left as it is, so files changed by hand are packed, not overwritten. Without a ROM
# or without Python it does nothing and says so: the game then reads the ROM itself.
cd "$(dirname "$0")/.." || exit 1
ROM="${1:-rom/Wonder Boy in Monster World (USA, Europe).md}"
PAK=assets/game.pak

PY=
for p in ${PYTHON:-python python3}; do         # (a python3 that is only the Store's stub fails here)
    if "$p" -c 'import sys; sys.exit(sys.version_info < (3, 8))' >/dev/null 2>&1; then PY=$p; break; fi
done
if [ -z "$PY" ]; then
    if [ -f "$PAK" ]; then echo "makepack: no Python 3 found: $PAK is kept as it is"
    else echo "makepack: no Python 3 found, so no $PAK: the game will read the ROM (see the README)"; fi
    exit 0
fi

extract() {
    if [ ! -f "$ROM" ]; then
        echo "makepack: no ROM at $ROM, so no $PAK: put your dump there and build again"
        exit 0
    fi
    echo "makepack: extracting the game's data from $ROM into assets/"
    "$PY" tools/extract.py "$ROM" assets --no-rooms >/dev/null || { echo "makepack: extracting failed"; exit 1; }
}

[ -f assets/manifest.json ] || extract
if [ -f "$PAK" ] && [ -z "$(find assets tools/extract.py tools/pack.py tools/rebuild.py tools/assetfmt.py tools/codetables.py \
        tools/midi.py -newer "$PAK" -type f ! -name game.pak 2>/dev/null | head -1)" ]; then
    exit 0                                      # up to date
fi
pack() {                                        # the music as MIDI first (assets/sound/midi), so the pack is the newest
    "$PY" tools/midi.py assets assets/sound/midi --effects >/dev/null || echo "makepack: the MIDI files failed (the pack goes on)"
    "$PY" tools/pack.py assets "$PAK"
}
pack
r=$?
if [ $r = 3 ]; then                             # assets/ from another version of the extractor
    extract
    pack || exit 1
elif [ $r != 0 ]; then
    exit 1
fi
