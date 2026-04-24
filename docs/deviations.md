# Review Deviations

Every decision that departs from the Knuth-level review's recommendation is
recorded here, with finding ID, what was done instead, reasoning, and the
reviewer who signed off.

Format:

```
## <finding-id> — short title

- **Commit.** <hash>
- **Reviewer.** <name / agent>
- **Review said.** <one line summary>
- **We did.** <one line summary>
- **Reasoning.** <why the deviation is justified>
- **Consequence.** <what a future reader should know>
```

## FIXTURE-MALFORMED — `fmt_size=0xFFFFFFFF` returns `UnsupportedFormat`, not `InvalidFormat`

- **Commit.** 4e8abfd (on `remediation/fixture-malformed`).
- **Reviewer.** orchestrator (self-recorded; to be cross-checked by invariant-agent).
- **Review said.** The FIXTURE-MALFORMED spec listed "fmt chunk claims `chunk_size = 0xFFFFFFFF`" as malformation #3 and implied the expected outcome was `InvalidFormat`.
- **We did.** The manifest for this blob records the observed outcome, `UnsupportedFormat`, and the `README.md` entry for the blob cites *why*: the chunk walker's integer arithmetic terminates cleanly on `chunk_size = 0xFFFFFFFF` (size_t promotion means no wrap on 64-bit), the inner fmt body is then parsed from the padded zeros, `audio_format` reads as `0`, and the parser returns `UnsupportedFormat` at `audio_file.cpp:285`.
- **Reasoning.** The invariant this fixture exercises is **INV-PARSER-REJECT**: "every malformed header returns either `InvalidFormat` or `UnsupportedFormat` in bounded time, with no sanitizer findings." Both return codes satisfy it. Forcing the parser to prefer `InvalidFormat` for this specific malformation would require extra special-case code whose only purpose is to make the test match a stronger specification than the invariant actually demands. That's a worse trade than accepting the observed behaviour as within-contract.
- **Consequence.** If a future change to `parse_wav_header` decides that `chunk_size > remaining-file-bytes` should be treated as `InvalidFormat` outright, this manifest entry flips to `InvalidFormat` and the test still passes. No call-site implications.

