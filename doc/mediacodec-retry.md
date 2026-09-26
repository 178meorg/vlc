# Android MediaCodec candidate retry

Video decoding tries compatible MediaCodec components in a stable, configurable
order. Startup failures advance to another component. Runtime codec failures
request a decoder-module reload, retaining the failed MIME/name combinations on
the current decoder object. Seeking does not clear this history; a new decoder
object starts with an empty history. When candidates are exhausted, normal VLC
module selection can fall back to software decoding, subject to the application's
existing codec configuration.

## Options

- `mediacodec-retry` (boolean, default true): enable startup and runtime retries.
  `--no-mediacodec-retry` selects only the first candidate and retains the legacy
  failure behavior, including no new Dolby-to-HEVC fallback. Candidate scoring
  and profile relaxation during initial selection still apply.
- `decoder-score-list` (string, default empty): comma-separated
  `POSIX-extended-regex=integer` rules, case-sensitive. The first matching rule
  determines a candidate's score. Unmatched candidates score 100. Negative
  scores disable a component before any MIME bonus is added; higher totals
  win, and equal totals retain collection order. Invalid entries are
  ignored with a warning. Patterns cannot contain commas; the last `=` in each
  entry separates the pattern from the score. For example:

  ```text
  --decoder-score-list=^c2\.vendor\.=200,^OMX\.bad\.=-1
  ```

  Scores do not override the component blacklist or the profile selection pass.
  `decoder-ignore-profile` continues to use its existing glob syntax.
- `mediacodec-fake-dolby-fail` (boolean, default false): simulate failure before
  configuring a Dolby Vision component, to exercise base HEVC fallback.

With retry enabled, Dolby Vision and base HEVC candidates share one sorted pool.
Dolby components that also declare HEVC support receive a bonus of 500; other
Dolby components receive 400; HEVC candidates receive no bonus. These three
groups are collected in that order, retaining Android enumeration order within
each group when totals tie. Scores can therefore put a base HEVC component ahead
of a Dolby component. Totals use 64-bit arithmetic.

Candidates are deduplicated by MIME/name, keeping the higher-scoring entry (the
first entry on ties). A component supporting both MIME types can be tried once
for each. There is no fixed candidate-count limit. The default blacklist remains
active even with custom scores. These are intentional differences from the
reference APK's name-only deduplication, 16-entry limit and configurable blacklist.

Video selection first collects the entire pool with the requested profiles. If
no eligible candidates remain, positive profiles are replaced by -1 for one
additional enumeration pass. Dolby requests already use -1 because the HEVC
base profile is not an Android Dolby profile. Blacklists, negative scores and
failed MIME/name records remain active in both passes. Enumeration errors abort
selection without returning a partial pool or triggering profile relaxation.
This only relaxes selection: input metadata and decoder configuration are not
rewritten. VC-1 keeps its existing MIME fallback before relaxing the final MIME.

HEVC candidates use ordinary color configuration and output metadata without
Dolby flags. This is a best-effort base-layer
fallback, not Dolby Vision color conversion; not all Dolby profiles produce a
correct picture without a Dolby decoder.

Runtime recovery does not seek backwards or replay already-consumed input. It
can lose frames and may need the next random-access frame to resume output.
Allocation/bitstream failures are not a reason to try every hardware component.
The audio MediaCodec option remains experimental and does not gain retries.

## Validation

`test_modules_codec_mediacodec_candidates` covers rule parsing, negative-score
exclusion, mixed-MIME bonuses, stable ordering, overflow, duplicate candidates,
MIME-specific exclusions and exhaustion across reloads. An injected enumerator
exercises the production selection loop, including whole-pool profile relaxation,
enumeration errors and allocation failures without leaking partial selections.
Existing MediaCodec profile and MKV Dolby tests should also
pass. The candidate test can be run on a POSIX host without an Android device:

```sh
cc -std=gnu11 -Wall -Wextra -Werror \
  test/modules/codec/mediacodec_candidates.c -o /tmp/test_mediacodec_candidates
/tmp/test_mediacodec_candidates
```

Device validation must additionally exercise both AImageReader and legacy
Surface output. Check H.264/HEVC normal playback; Dolby playback with and without
the fake failure option; startup create/configure/start failures; input/output
queue and codec-restart failures before and after the first picture; exhaustion
with software fallback; retry disabled; and pause, seek and stop during recovery.
Observe candidate name/MIME/score and failure-stage logs. Verify old codecs are
released before replacements start and there are no late reader callbacks,
duplicate buffer releases or hangs. Host policy tests and cross-compilation do
not replace these playback checks.
