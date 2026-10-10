# Rebuild the ROM from generated source and fail unless it is byte-identical.
# Override the tools with PYTHON=... VASM=... and the ROM path with ROM=...
ROM ?= rom/Wonder Boy in Monster World (USA, Europe).md

.PHONY: all gfx clean
all:
	sh tools/build.sh "$(ROM)"

gfx:
	$${PYTHON:-python3} -I tools/gfx.py "$(ROM)" --png work/gfx_all.png

clean:
	rm -rf work
