# Generic file codec architecture

Status: design contract, not an implemented ABI. Define the wire and library
ABI with the first real codec, then test it with at least two unrelated media
families before declaring it stable.

## One mechanism for file formats

Astra discovers installable userspace codec providers. A provider advertises
stable format identifiers, decode and/or encode capability, and the type of
output it produces or accepts. The registry and lifecycle are generic: audio,
images, video, archives, documents, and future encoded data use the same
discovery and stream contract. Typed libraries interpret the result: Audio Kit
receives PCM frames, Graphics Kit receives image surfaces, and a future Video
Kit receives timestamped frames. A codec does not become a kernel driver and
applications do not need one hardcoded branch per format.

The provider contract has these operations:

1. **Probe** a bounded byte prefix and optional file-format metadata without
   consuming the source. Return a format identifier and whether the bytes
   match, not a guess based on the filename.
2. **Open** a decoder or encoder against a byte source/sink and negotiate a
   typed output/input description (sample format, image dimensions and pixel
   format, timed video tracks, etc.).
3. **Transfer** in bounded chunks with explicit progress, end-of-stream,
   backpressure, seek requirements, and error results. No whole-file load is
   required to use a codec.
4. **Close/cancel** and release every stream resource even after a malformed
   file, provider crash, or peer loss.

Read and write are separate advertised capabilities. A decoder for MP3 does
not imply an MP3 encoder. Providers may use shared libraries or isolated
processes behind this contract; untrusted complex parsers should run in a
protected process so their failure cannot take down the desktop or kernel.
The host may perform expensive decoding where target measurements justify it,
but applications still see one native Astra interface and the same format
selection behavior.

## Format identity

Filesystem metadata may store a format identifier, but it is only a hint.
The registry uses metadata to narrow candidates and validated magic or
container headers to confirm a match. Each selected codec validates its full
stream. A missing extension, renamed file, or misleading extension does not
change the decoded format. A metadata/header conflict, ambiguous match, or
unknown format is reported explicitly; Astra never silently forces bytes
through a claimed codec. Extensions may be shown to users but have no
authority in codec selection.

Probe reads are bounded and side-effect-free. Selection is deterministic when
multiple providers recognize a format, with installed provider policy visible
to the user. Codec packages are versioned and registered/unregistered with
the normal package mechanism; no kernel update is needed to learn a format.

## First implementation gate

Build one small generic registry and streaming contract with the first WAV/PCM
provider, then prove that a second, structurally different provider uses it
without changing the registry. Test correct metadata and magic, missing and
misleading extensions, metadata/header conflicts, truncated and hostile
headers, unknown formats, partial streams, provider failure, and cleanup.
Keep format-specific rules inside providers; do not freeze a universal codec
ABI from this document alone.
