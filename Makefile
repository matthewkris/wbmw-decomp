# Rebuild the ROM from generated source and fail unless it is byte-identical.
# Override the tools with PYTHON=... VASM=... and the ROM path with ROM=...
ROM ?= rom/Wonder Boy in Monster World (USA, Europe).md

.PHONY: all clean
all:
	sh tools/build.sh "$(ROM)"

clean:
	rm -rf work
