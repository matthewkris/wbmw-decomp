# Wonder Boy in Monster World (Genesis) — decompilation and port

Work-in-progress reverse engineering of the Genesis game, aimed at a native C port.
The repository holds tools and notes only. No ROM data is committed; supply your own dump.

Expected ROM: `Wonder Boy in Monster World (USA, Europe).md`, 786,432 bytes,
SHA-1 `87a968f773c7e807e647c0737132457b06b78276`.

    pip install capstone
    tools/fixpoint.sh "rom/Wonder Boy in Monster World (USA, Europe).md"

That writes `work/map.json` (code/data map) and `work/code_000200_01D000.lst` (listing).
See `docs/NOTES.md` for what is known so far.
