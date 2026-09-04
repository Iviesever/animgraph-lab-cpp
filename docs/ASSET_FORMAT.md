# Asset format

All formats are little-endian version 1, bounded to 64 MiB, field-encoded, and
protected by CRC32 with the CRC field treated as zero during calculation. Unknown
magic/version/endian, truncation, bad lengths/offsets/counts, CRC mismatch, invalid
floats, and invalid hierarchy fail closed. C++ struct memory is never persisted.

| Format | Magic | Header | Payload |
|---|---|---:|---|
| `.agskel` | `AGSKEL1\0` | 40 B | fixed joint records + string table |
| `.agclip` | `AGCLIP1\0` | 80 B | track/event/marker records + key arrays + strings |
| `.aggraph` | `AGGRAPH1` | 36 B | canonical Graph Plan JSON |

Skeleton records contain compiled parent, original index, name/semantic references,
and two explicit 10-float Transforms. Clip records contain absolute array offsets
and counts; strings use relative offset/length pairs. Graph envelopes store payload
length, node count, and checksum. Their non-recursive canonical-schema validator
checks exact field order, node/config enum shapes, arity, slot initialization,
state layout, parameter indices, sync bindings, numeric ranges, and node count
without accepting decoy fields or unbounded JSON nesting.

Round trips are byte-stable. `animc compile`, `inspect`, and `validate` exercise the
same codecs used by tests and fuzzing.
