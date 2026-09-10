# Astra text and Unicode contract

Status: normative, 2026-09-10

## Encoding

UTF-8 is the sole text encoding throughout Astra OS. Public text APIs use
explicit byte spans where text may contain U+0000 and null-terminated strings
where the surrounding ABI requires them. A byte buffer is binary only when its
API explicitly says so.

ASCII identifiers such as assign names, protocol names, BCP 47 language tags,
and environment-variable names remain restricted grammars. ASCII is a subset
of UTF-8; those restrictions do not create a second system encoding. Astra
does not use the process locale to interpret bytes.

The NDK's `astra/utf8.h` is the sole validation, scalar decode/encode, and
scalar-boundary authority. Subsystems must not carry private UTF-8 decoders or
validators.

## Boundaries and malformed input

- NDK calls and IPC receivers validate complete text before publishing it.
- Paths, filenames, launch arguments, environment values, window titles,
  labels, alerts, manifests, and configuration text reject malformed UTF-8.
- Streaming decoders, such as a terminal receiving arbitrary output bytes,
  may replace malformed sequences with U+FFFD because a complete validated
  span does not exist at that boundary. The replacement behavior is part of
  that API's contract, never an implicit fallback for stored text.
- Binary file contents, network packets, images, audio, and device buffers are
  not text and are never UTF-8 validated.
- Input text events carry one Unicode scalar value. Physical/editing keys use
  command values above U+10FFFF, so no Unicode character can collide with a
  key command.

## Indexing, editing, and display

Stored offsets are UTF-8 byte offsets. Decoders, carets, deletion, selection,
and slicing must not split an encoded scalar. User-facing editing and cursor
motion operate on extended grapheme clusters so combining marks, emoji
sequences, and joined scripts behave as one visible unit. Text layout may
further group scalars into shaped glyph clusters; glyph count is never assumed
to equal scalar count.

UTF-8 does not imply glyph coverage, shaping, bidirectional layout, line
breaking, or normalization. Those are font/text-layout responsibilities.

## Normalization and comparison

Astra does not silently normalize text at API boundaries. Filesystem names are
validated UTF-8 and compared byte-exactly unless a mounted filesystem declares
a different comparison contract. This keeps one spelling from changing merely
because it crossed VFS or IPC and preserves backend semantics.

Search, cataloguing, and user-facing matching may maintain normalized and
case-folded index keys while retaining the original UTF-8 bytes for display and
opening. Such indexes must identify their Unicode data version.

## Clipboard and interchange

Plain clipboard text is `text/plain;charset=utf-8`. Rich formats may accompany
it, but every text control must be able to provide and consume the plain UTF-8
form. External legacy encodings are converted by explicit importers; they do
not leak into application or service ABIs.
