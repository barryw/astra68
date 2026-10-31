#ifndef ASTRA_HOST_H
#define ASTRA_HOST_H

/**
 * @file host.h
 * @brief Stable bulk transport between Astra services and an attached host.
 *
 * The kernel validates authority and DMA ownership but never interprets a
 * command.  Host descriptors, pointers and errno values never cross this
 * boundary.  Command classes are append-only; batching is part of version 1
 * so hardware and software implementations use the same data plane.
 */

#include <astra/compiler.h>
#include <stddef.h>
#include <stdint.h>

#include <astra/message_abi.h>
#include <astra/syscall.h>
#include <astra/volume_abi.h>

/** Native-big-endian `HACC` host device class, matched by device lookup. */
#define ASTRA_DEVICE_CLASS_HOST UINT32_C(0x48414343) /* HACC */
/** Identifier of the one host device instance. */
#define ASTRA_DEVICE_ID_HOST0   UINT32_C(0x48410001)
/** Capability name under which the host device lease is granted. */
#define ASTRA_CAPABILITY_HOST_DEVICE "HOST_DEVICE"
/** The host device with ::ASTRA_RIGHT_HOST_SCOPE: the filesystem
 * service's grant. Every other holder is granted HOST_DEVICE without it. */
#define ASTRA_CAPABILITY_HOST_FILESYSTEM "HOST_FILESYSTEM"

/** Current host transport protocol version, as reported by HOST_QUERY. */
#define ASTRA_HOST_VERSION     UINT32_C(0x00030000)
/** The host exposes the filesystem service. */
#define ASTRA_HOST_CAP_FILESYSTEM (1u << 0)
/** Filesystem commands are scoped to the submitting process's authenticated owner. */
#define ASTRA_HOST_CAP_OWNER_SCOPED (1u << 1)
/** The host accepts the single-doorbell `AstraHostSubmission` descriptor for HOST_EXECUTE. */
#define ASTRA_HOST_CAP_SUBMISSION_DESCRIPTOR (1u << 2)
/** The host supports persistent HOST_CHANNEL_OPEN transport channels. */
#define ASTRA_HOST_CAP_CHANNEL (1u << 3)
/** A persistent channel can arm an interrupt instead of being polled with HOST_CHANNEL_WAIT. */
#define ASTRA_HOST_CAP_CHANNEL_ARMED_IRQ (1u << 4)
/** The host exposes the metrics service. */
#define ASTRA_HOST_CAP_METRICS (1u << 5)
/** The host exposes the remote-desktop service. */
#define ASTRA_HOST_CAP_REMOTE_DESKTOP (1u << 6)
/** The host exposes the entropy service. */
#define ASTRA_HOST_CAP_ENTROPY (1u << 7)
/** The host exposes the audio service. */
#define ASTRA_HOST_CAP_AUDIO (1u << 8)
/** Set in ::AstraHostLeaseInfo.capabilities when the host plays application
 * audio streams (%audio_stream.h, ::ASTRA_SYSCALL_AUDIO_STREAM_OPEN). */
#define ASTRA_HOST_CAP_AUDIO_STREAM (1u << 9)
/** Host channels may be bound to filesystem scopes
 * (::ASTRA_SYSCALL_HOST_SCOPE_CREATE). On a bound channel the provider
 * runs only filesystem commands, each through one scope the kernel
 * attached: beneath its root, with its rights. */
#define ASTRA_HOST_CAP_FS_SCOPE (1u << 10)
/** Filesystem commands use ::ASTRA_HOST_COMMAND_VERSION: one claimed owner, rooted
 * sessions, path operands in the data span and the AstraHostNode record. */
#define ASTRA_HOST_CAP_FS_SESSIONS (1u << 11)
/** Ordered dot and symlink resolution, with rooted namespace continuations. */
#define ASTRA_HOST_CAP_FS_RESOLVES (1u << 12)
/** Provider-owned position, status flags and shared open descriptions. */
#define ASTRA_HOST_CAP_FS_DESCRIPTIONS (1u << 13)
/** READDIR returns a bounded packed batch under one description lock. */
#define ASTRA_HOST_CAP_FS_DIRECTORY_BATCH (1u << 14)
/** Report the current confined path of a retained directory. */
#define ASTRA_HOST_CAP_FS_PATHS (1u << 15)
/** Public volume metadata does not confer a rooted file or control grant. */
#define ASTRA_HOST_CAP_VOLUME_CATALOG (1u << 16)
/** Host converts a bounded outline source into validated AFNT for Fonts. */
#define ASTRA_HOST_CAP_FONT_CONVERT (1u << 17)
/** Host decodes bounded encoded media streams through isolated providers. */
#define ASTRA_HOST_CAP_CODEC (1u << 18)
/** Channel configuration: the scope words name a scope. Kernel-written. */
#define ASTRA_HOST_SCOPE_BOUND     (1u << 0)
/** The scope refuses every mutation (ASTRA_STATUS_READ_ONLY). */
#define ASTRA_HOST_SCOPE_READ_ONLY (1u << 1)
/** Kept by the kernel, never sent to the provider: the scope may be handed
 * on, so it arrives with ::ASTRA_RIGHT_TRANSFER. Without it a scope
 * arrives without TRANSFER: its holder may use it and fork with it. */
#define ASTRA_HOST_SCOPE_DELEGATE  (1u << 2)
/** Scopes one host channel carries at most. */
#define ASTRA_HOST_CHANNEL_SCOPE_MAX 8u
/** Set in ::AstraHostLeaseInfo.state_flags once the host lease is ready for commands. */
#define ASTRA_HOST_STATE_READY    (1u << 0)

/** AstraHostCommand::service selecting the filesystem service. */
#define ASTRA_HOST_SERVICE_FILESYSTEM UINT16_C(1)
/** AstraHostCommand::service selecting the metrics service. */
#define ASTRA_HOST_SERVICE_METRICS UINT16_C(2)
/** AstraHostCommand::service selecting the remote-desktop service. */
#define ASTRA_HOST_SERVICE_REMOTE_DESKTOP UINT16_C(3)
/** AstraHostCommand::service selecting the entropy service. */
#define ASTRA_HOST_SERVICE_ENTROPY UINT16_C(4)
/** AstraHostCommand::service selecting the audio service. */
#define ASTRA_HOST_SERVICE_AUDIO UINT16_C(5)
/** Native volume metadata and lifecycle transport. */
#define ASTRA_HOST_SERVICE_VOLUMES UINT16_C(6)
/** Private conversion transport for the resident Fonts service. */
#define ASTRA_HOST_SERVICE_FONTS UINT16_C(7)
/** Generic encoded-media stream transport. */
#define ASTRA_HOST_SERVICE_CODEC UINT16_C(8)
/** Bytes in every CODEC result event before optional decoded output. */
#define ASTRA_HOST_CODEC_RESULT_BYTES 100u
/** CODEC uses one worker per stream. OPEN supplies source length in offset and
 * returns a stream handle in result_value; later operations supply that handle.
 * SOURCE supplies requested source bytes at offset; READ asks for one bounded
 * output chunk; SEEK supplies a timestamp in nanoseconds; CLOSE retires the
 * worker. Every operation except CLOSE returns an
 * ASTRA_HOST_CODEC_RESULT_BYTES big-endian worker event followed by any
 * decoded output bytes in the command data span. */
enum {
    ASTRA_HOST_CODEC_OPEN = 1u,
    ASTRA_HOST_CODEC_SOURCE,
    ASTRA_HOST_CODEC_READ,
    ASTRA_HOST_CODEC_SEEK,
    ASTRA_HOST_CODEC_CLOSE
};
/** One source per channel. BEGIN carries source size in value_lo, name and
 * license byte lengths in offset_lo/offset_hi, and concatenated text in data.
 * DATA carries source bytes at offset_lo; CONVERT takes face index in value_lo
 * and answers AFNT size in result_value and collection face count in value_hi.
 * READ returns AFNT bytes at offset_lo; CLOSE discards all session state. */
enum {
    ASTRA_HOST_FONT_BEGIN = 1u,
    ASTRA_HOST_FONT_DATA,
    ASTRA_HOST_FONT_CONVERT,
    ASTRA_HOST_FONT_READ,
    ASTRA_HOST_FONT_CLOSE
};
/** Query a coherent AstraVolumeCatalogPage using an AstraVolumeCatalogQuery. */
#define ASTRA_HOST_VOLUME_CATALOG UINT16_C(1)
/** Submit a typed AstraHostVolumeControl through the claimed provider. */
#define ASTRA_HOST_VOLUME_CONTROL UINT16_C(2)
/** Query a coherent AstraVolumeUsagePage using an AstraVolumeUsageQuery. */
#define ASTRA_HOST_VOLUME_USAGE UINT16_C(3)
/** Claimed-provider-only notification that canonical native control-session
 * holders changed. All operands and payload lengths are zero. It advances the
 * same usage mutation sequence as description/session changes. */
#define ASTRA_HOST_VOLUME_HOLDERS_CHANGED UINT16_C(4)
/** Provider-only exact physical-source format or prior-generation status. */
#define ASTRA_HOST_VOLUME_FORMAT UINT16_C(5)
/** Start provider format. */
#define ASTRA_HOST_VOLUME_FORMAT_START UINT32_C(1)
/** Query provider format status. */
#define ASTRA_HOST_VOLUME_FORMAT_STATUS UINT32_C(2)
/** Provider-only exact physical-device partition-map edit or status. */
#define ASTRA_HOST_VOLUME_PARTITION UINT16_C(6)
/** Start provider disk-map edit. */
#define ASTRA_HOST_VOLUME_PARTITION_START UINT32_C(1)
/** Query provider disk-map edit status. */
#define ASTRA_HOST_VOLUME_PARTITION_STATUS UINT32_C(2)
/** Provider-only lifecycle request. requester is kernel-stamped by HostFS's
 * receive endpoint, never accepted from an application payload. */
typedef struct AstraHostVolumeControl {
    AstraVolumeControl control; /**< Exact incarnation, operation and resource limits. */
    uint32_t requester; /**< Kernel-authenticated native requesting process. */
    uint32_t reserved; /**< Zero. */
} AstraHostVolumeControl;
/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraHostVolumeControl) == 64u, "host volume control ABI");
typedef struct AstraHostVolumeFormat {
    AstraVolumeReference source;
    uint32_t operation;
    uint32_t format;
    char label[12];
    uint32_t requester;
    uint32_t reserved;
    uint32_t reserved2;
} AstraHostVolumeFormat;
_Static_assert(sizeof(AstraHostVolumeFormat) == 56u, "host volume format ABI");
typedef struct AstraHostVolumePartition {
    AstraVolumeReference device;
    uint32_t mode;
    uint32_t requester;
    AstraVolumePartitionEdit edit;
    uint32_t reserved[2];
} AstraHostVolumePartition;
_Static_assert(sizeof(AstraHostVolumePartition) == 72u, "host volume partition ABI");
/** @endcond */
/** Every SESSION_OPEN supplies this typed provider-only payload. Parent
 * sessions use a zero reference; requester is authenticated by HostFS IPC. */
typedef struct AstraHostSessionOpen {
    AstraVolumeReference volume; /**< Exact root mount; zero when deriving from a session. */
    uint32_t requester; /**< Kernel-authenticated native requesting process. */
    uint32_t reserved; /**< Zero. */
} AstraHostSessionOpen;
/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraHostSessionOpen) == 32u, "host session open ABI");
/** @endcond */
/** Bytes of the largest path buffer, terminator included: a path operand
 * on the wire is at most ASTRA_HOST_FS_PATH_MAX - 1 bytes, unterminated. */
#define ASTRA_HOST_FS_PATH_MAX 4096u

/** Filesystem operations, AstraHostCommand::operation when service is
 * ::ASTRA_HOST_SERVICE_FILESYSTEM. Every path operation resolves its
 * `path` beneath `handle`, an open directory, or beneath the session root
 * when `handle` is 0; with `handle` 0 the path is absolute ("/" names the
 * root), otherwise relative. */
enum {
    ASTRA_HOST_FS_OPEN = 1u,        /**< Open a path; `handle` answers the new handle. */
    ASTRA_HOST_FS_CLOSE,            /**< Close `handle`. */
    ASTRA_HOST_FS_READ,             /**< Read `handle` at offset_hi/offset_lo. */
    ASTRA_HOST_FS_WRITE,            /**< Write at offset or append; value is exclusive maximum on input, resulting position on output. */
    ASTRA_HOST_FS_SYNC,             /**< fsync(2) `handle`. */
    ASTRA_HOST_FS_TRUNCATE,         /**< Set `handle`'s length to value_hi/value_lo. */
    ASTRA_HOST_FS_STAT,             /**< No-follow metadata of a path, or of `handle` itself when the path is empty. */
    ASTRA_HOST_FS_READDIR,          /**< Packed native dirents: value_lo bounds entries, result_length bytes, value answers next cookie. */
    ASTRA_HOST_FS_MKDIR,            /**< Create a directory; value_lo is its mode. */
    ASTRA_HOST_FS_UNLINK,           /**< Remove a path; ASTRA_HOST_FS_UNLINK_* in flags. */
    ASTRA_HOST_FS_RENAME,           /**< Rename `path` (beneath `handle`) to `path2` (beneath directory handle value_lo). */
    ASTRA_HOST_FS_CHMOD,            /**< Change a path's mode, or `handle`'s when the path is empty. */
    ASTRA_HOST_FS_READLINK,         /**< Read a symbolic link's stored target into the data span. */
    ASTRA_HOST_FS_SYMLINK,          /**< Create `path2` (beneath `handle`) holding the literal target `path`. */
    ASTRA_HOST_FS_LINK,             /**< Hard-link `path` (beneath `handle`) as `path2` (beneath value_lo). */
    ASTRA_HOST_FS_FILESYSTEM_INFO,  /**< ::AstraHostFilesystemInfo for a path or `handle`. */
    ASTRA_HOST_FS_SESSION_OPEN,     /**< Open a rooted session; result_value answers its id. */
    ASTRA_HOST_FS_SESSION_CLOSE,    /**< Close `session`. */
    ASTRA_HOST_FS_CLAIM,            /**< Bind the filesystem service to the submitting owner, or release it. */
    ASTRA_HOST_FS_SEEK,             /**< Signed offset, flags 0/1/2: begin/current/end; value answers position. */
    ASTRA_HOST_FS_SHARE,            /**< Retain handle's description; value answers a one-use token. */
    ASTRA_HOST_FS_ADOPT,            /**< Consume offset's token in this session; handle answers the new handle. */
    ASTRA_HOST_FS_FILE_FLAGS,       /**< flags masks mutable status, value_lo supplies it; value_lo answers open flags. */
    ASTRA_HOST_FS_PATH              /**< Current directory path beneath the session root, or NOT_FOUND when unnamed. */
};
/** Highest valid ASTRA_HOST_FS_* operation code. */
#define ASTRA_HOST_FS_MAX ASTRA_HOST_FS_PATH

/** A root SESSION_OPEN carries an exact AstraVolumeReference in its input
 * payload. Derived sessions carry no target payload and inherit their parent.
 * The catalog supplies identity; it never supplies data authority. */
/** WRITE flag: append at end of file, atomically, for this write. */
#define ASTRA_HOST_FS_WRITE_APPEND      (1u << 15)
/** READ/WRITE at the shared position; value answers the position after. */
#define ASTRA_HOST_FS_IO_SEQUENTIAL     (1u << 14)
/** ADOPT withdraws an unclaimed token; only its sharer may withdraw. */
#define ASTRA_HOST_FS_ADOPT_DISCARD     (1u << 0)
/** UNLINK flag: remove an empty directory; zero flags require a non-directory. */
#define ASTRA_HOST_FS_UNLINK_DIRECTORY  (1u << 0)
/** RENAME flag: refuse an occupied destination. */
#define ASTRA_HOST_FS_RENAME_NOREPLACE (1u << 0)
/** SESSION_OPEN flag: every mutation in the session is refused
 * (ASTRA_STATUS_READ_ONLY); a session derived from a read-only one must
 * carry it too. */
#define ASTRA_HOST_FS_SESSION_READ_ONLY (1u << 0)
/** STAT flag: follow the final symbolic link. */
#define ASTRA_HOST_FS_STAT_FOLLOW (1u << 0)
/** STAT flag: permit a missing final leaf, returning zero node metadata. */
#define ASTRA_HOST_FS_STAT_MISSING (1u << 1)
/** STAT flag: offset_lo carries the remaining link budget; value_hi answers
 * the links followed. Without this flag the provider uses its 40-link limit. */
#define ASTRA_HOST_FS_STAT_BUDGET (1u << 2)
/** Resolve the final directory entry without following it, even with '/'. */
#define ASTRA_HOST_FS_STAT_ENTRY (1u << 3)
/** ESCAPED result: a parent component left the session root. */
#define ASTRA_HOST_FS_ESCAPE_PARENT 1u
/** ESCAPED result: a symbolic link left the session root. */
#define ASTRA_HOST_FS_ESCAPE_SYMLINK 2u
/** ESCAPED replies put the original byte position in result_value, the
 * reason in value_lo, links followed in value_hi, and the complete absolute
 * Astra namespace continuation in data (result_length bytes, no NUL).
 * No outside host path is followed. SESSION_OPEN refuses escapes with ACCESS;
 * RENAME and LINK refuse them with CROSS_DEVICE. */
/** CLAIM flag: release the claim and close every session and handle of
 * the owner. CLAIM is honoured only on a channel the kernel opened with
 * ASTRA_HOST_CHANNEL_PROVIDER; anywhere else it answers ACCESS. */
#define ASTRA_HOST_FS_CLAIM_RELEASE     (1u << 0)

/** AstraHostCommand::operation: fetch an ::AstraHostMetricsSnapshot into the
 * command's data span; the metrics service's only operation.
 */
#define ASTRA_HOST_METRICS_SNAPSHOT UINT16_C(1)
/** Wire-format version the host stamps into AstraHostMetricsSnapshot::version. */
#define ASTRA_HOST_METRICS_VERSION UINT16_C(5)

/** AstraHostCommand::operation: connect to the remote-desktop session
 * (idempotent if already connected) and receive its next queued event.
 */
#define ASTRA_HOST_REMOTE_DESKTOP_ACQUIRE UINT16_C(1)
/** AstraHostCommand::operation: receive the remote-desktop session's next
 * queued event without connecting.
 */
#define ASTRA_HOST_REMOTE_DESKTOP_STATUS UINT16_C(2)

/** Fill the command data span with host cryptographic entropy. */
#define ASTRA_HOST_ENTROPY_FILL UINT16_C(1)
/** Host entropy requests follow the POSIX getentropy(3) bound. */
#define ASTRA_HOST_ENTROPY_MAX UINT32_C(256)

/** Host PCM transport: 48 kHz stereo, with the input format selected at open.
 * A converter (CONVERT_OPEN: value_lo source format, value_hi target
 * format) turns PCM into another format and rate on the host and hands it
 * back: CONVERT takes data_length source bytes (value_lo bit 0: no more
 * follow), returns up to data_capacity target bytes in the same data area
 * (result_length) and the target frames still ready (result_value).
 * CLOSE ends either kind of handle.
 */
enum {
    ASTRA_HOST_AUDIO_OPEN = 1u,       /**< Open a PCM playback voice. */
    ASTRA_HOST_AUDIO_WRITE,           /**< Queue PCM frames for playback. */
    ASTRA_HOST_AUDIO_GAIN,            /**< Set a voice's playback gain. */
    ASTRA_HOST_AUDIO_STATUS,          /**< Fetch an ::AstraHostAudioStatus for a voice. */
    ASTRA_HOST_AUDIO_CLOSE,           /**< Close a PCM voice, MIDI voice, or converter handle. */
    ASTRA_HOST_AUDIO_FINISH,          /**< Signal end of stream and drain queued frames. */
    ASTRA_HOST_AUDIO_PAUSE,           /**< Pause or resume a voice. */
    ASTRA_HOST_AUDIO_CLEAR,           /**< Discard a voice's queued frames without closing it. */
    ASTRA_HOST_AUDIO_CONVERT_OPEN,    /**< Open a PCM format/rate converter. */
    ASTRA_HOST_AUDIO_CONVERT,         /**< Convert queued source bytes to the target format. */
    ASTRA_HOST_AUDIO_FONT_QUERY,      /**< Check whether the host already holds a SoundFont by digest. */
    ASTRA_HOST_AUDIO_FONT_BEGIN,      /**< Open a SoundFont upload handle. */
    ASTRA_HOST_AUDIO_FONT_DATA,       /**< Carry the next chunk of an uploading SoundFont. */
    ASTRA_HOST_AUDIO_FONT_END,        /**< Finish a SoundFont upload, verifying size and digest. */
    ASTRA_HOST_AUDIO_MIDI_OPEN,       /**< Open a MIDI synth voice with the default SoundFonts. */
    ASTRA_HOST_AUDIO_MIDI_FONT,       /**< Layer a previously sent SoundFont onto a MIDI voice. */
    ASTRA_HOST_AUDIO_MIDI_LOAD,       /**< Load a Standard MIDI File into a voice, in ordered chunks. */
    ASTRA_HOST_AUDIO_MIDI_PLAY,       /**< Start playback of a loaded song. */
    ASTRA_HOST_AUDIO_MIDI_STOP,       /**< Stop the playing song. */
    ASTRA_HOST_AUDIO_MIDI_STATUS,     /**< Fetch an ::AstraHostMidiStatus for a MIDI voice. */
    ASTRA_HOST_AUDIO_MIDI_SYSTEM_FONT,/**< Layer a shared SoundFont onto a MIDI voice. */
    ASTRA_HOST_AUDIO_FONT_LIST,       /**< List the shared SoundFonts available on the host. */
    ASTRA_HOST_AUDIO_MIDI_PRESETS,    /**< List the presets available across a voice's loaded fonts. */
    ASTRA_HOST_AUDIO_MIDI_EVENTS,     /**< Play one or more short MIDI channel messages immediately. */
    ASTRA_HOST_AUDIO_MIDI_SET         /**< Change one MIDI synthesis setting (ASTRA_HOST_MIDI_SET_*). */
};
/** Highest valid ASTRA_HOST_AUDIO_* operation code. */
#define ASTRA_HOST_AUDIO_OPERATION_MAX ASTRA_HOST_AUDIO_MIDI_SET
/** AstraHostCommand::value_lo bit for ASTRA_HOST_AUDIO_CONVERT: no more source bytes follow. */
#define ASTRA_HOST_AUDIO_CONVERT_END 1u

/* SoundFonts and MIDI synthesis on the host. Shared fonts are files in the
 * SOUND volume's soundfonts/ directory, which the host reads directly, and
 * soundfonts/default lists, one file name a line, the fonts every voice
 * starts with, lowest first. A program's own font is sent instead and
 * named by the SHA-256 of its bytes: FONT_QUERY (data: the digest)
 * succeeds when the host holds it; FONT_BEGIN (value_lo: size; data: the
 * digest, or none to have the host compute it) opens an upload handle;
 * FONT_DATA (value_lo: offset) carries the bytes in order; FONT_END checks
 * size and digest, keeps the font, and returns its digest as data.
 * MIDI_OPEN (no data) opens a synth voice with the default fonts;
 * MIDI_SYSTEM_FONT (data: a shared font's file name) and MIDI_FONT (data:
 * a sent font's digest) add one more font on top, a preset in a later font
 * hiding the same bank and program in an earlier one; MIDI_LOAD (value_lo: offset, value_hi: total size) takes a Standard
 * MIDI File in order; MIDI_PLAY (value_lo: plays, ASTRA_HOST_MIDI_FOREVER
 * repeats); PAUSE, GAIN and CLOSE act as on a PCM voice; MIDI_STOP ends
 * the song; MIDI_STATUS returns an AstraHostMidiStatus as data.
 *
 * FONT_LIST (no handle; value_lo: the first index) returns
 * AstraHostAudioFontRecords for the shared fonts in name order, as many as
 * fit, and the total in result_value. MIDI_PRESETS (value_lo: the first
 * index) does the same with AstraHostMidiPresets for every preset the
 * voice's fonts provide, by bank and program; until the voice has loaded
 * every font it was given it answers BUSY. MIDI_EVENTS (data: short MIDI
 * messages, ASTRA_HOST_MIDI_EVENT_BYTES each) plays them at once, in order
 * after every earlier request. MIDI_SET (value_lo: an
 * ASTRA_HOST_MIDI_SET_* setting, value_hi: its value) changes one. The
 * records below travel as bytes: every field is big-endian, as the guest
 * reads it, whoever writes it.
 */
/** Bytes in a SoundFont's or sent font's SHA-256 digest. */
#define ASTRA_HOST_AUDIO_DIGEST_BYTES 32u
/** Maximum bytes, including the terminating NUL, of a SoundFont file name. */
#define ASTRA_HOST_FONT_NAME_MAX 128u
/** Maximum byte size of one Standard MIDI File accepted by MIDI_LOAD. */
#define ASTRA_HOST_MIDI_SONG_MAX (1u << 20)
/** Maximum byte size of one sent SoundFont accepted by FONT_BEGIN/FONT_END. */
#define ASTRA_HOST_FONT_MAX (128u << 20)
/** MIDI_PLAY play count that repeats the song forever. */
#define ASTRA_HOST_MIDI_FOREVER UINT32_C(0xffffffff)
/** Bytes in one MIDI_EVENTS channel message: status, data1, data2, zero, as on the wire. */
#define ASTRA_HOST_MIDI_EVENT_BYTES 4u
/** Maximum bytes, including the terminating NUL, of an AstraHostMidiPreset name. */
#define ASTRA_HOST_MIDI_PRESET_NAME_MAX 24u
/** AstraHostAudioFontRecord::flags bit: one of the fonts every voice starts with. */
#define ASTRA_HOST_AUDIO_FONT_DEFAULT (1u << 0)

/** MIDI_SET's settings, carried in AstraHostCommand::value_lo. Levels and
 * times are in thousandths.
 */
enum {
    ASTRA_HOST_MIDI_SET_REVERB = 1u,       /**< 0 off, 1 on. */
    ASTRA_HOST_MIDI_SET_REVERB_ROOM,       /**< 0..1000. */
    ASTRA_HOST_MIDI_SET_REVERB_DAMP,       /**< 0..1000. */
    ASTRA_HOST_MIDI_SET_REVERB_WIDTH,      /**< 0..100000. */
    ASTRA_HOST_MIDI_SET_REVERB_LEVEL,      /**< 0..1000. */
    ASTRA_HOST_MIDI_SET_CHORUS,            /**< 0 off, 1 on. */
    ASTRA_HOST_MIDI_SET_CHORUS_VOICES,     /**< 0..99. */
    ASTRA_HOST_MIDI_SET_CHORUS_LEVEL,      /**< 0..10000. */
    ASTRA_HOST_MIDI_SET_CHORUS_SPEED,      /**< mHz, 100..5000. */
    ASTRA_HOST_MIDI_SET_CHORUS_DEPTH,      /**< microseconds, 0..256000. */
    ASTRA_HOST_MIDI_SET_POLYPHONY,         /**< voices, 1..65535. */
    ASTRA_HOST_MIDI_SET_TEMPO,             /**< the song's tempo, 1..100000. */
    ASTRA_HOST_MIDI_SET_POSITION           /**< seek the song to this tick. */
};
/** Highest valid ASTRA_HOST_MIDI_SET_* setting. */
#define ASTRA_HOST_MIDI_SET_MAX ASTRA_HOST_MIDI_SET_POSITION

/** One shared SoundFont, as listed by ASTRA_HOST_AUDIO_FONT_LIST. */
typedef struct AstraHostAudioFontRecord {
    uint32_t flags;                       /**< ASTRA_HOST_AUDIO_FONT_* bits. */
    uint32_t bytes_hi;                    /**< Font file size in bytes, upper 32 bits. */
    uint32_t bytes_lo;                    /**< Font file size in bytes, lower 32 bits. */
    char name[ASTRA_HOST_FONT_NAME_MAX];  /**< NUL-terminated file name. */
} AstraHostAudioFontRecord;

/** One MIDI preset, as listed by ASTRA_HOST_AUDIO_MIDI_PRESETS. */
typedef struct AstraHostMidiPreset {
    uint16_t bank;                        /**< General MIDI bank number. */
    uint8_t program;                      /**< General MIDI program number. */
    uint8_t font;                         /**< Its font's place in the stack. */
    char name[ASTRA_HOST_MIDI_PRESET_NAME_MAX]; /**< NUL-terminated preset name. */
} AstraHostMidiPreset;

/** A MIDI voice's playback state, returned by ASTRA_HOST_AUDIO_MIDI_STATUS. */
typedef struct AstraHostMidiStatus {
    uint32_t sounding;                    /**< Playing, or notes still ring. */
    uint32_t position_ticks;              /**< Current position in the song, in ticks. */
    uint32_t length_ticks;                /**< The loaded song's length, in ticks. */
    uint32_t ticks_per_quarter;           /**< Zero with no song loaded. */
    uint32_t tempo_us_per_quarter;        /**< Current tempo, in microseconds per quarter note. */
    uint32_t fonts_loading;               /**< Fonts given to the voice but not yet ready. */
} AstraHostMidiStatus;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraHostAudioFontRecord) == 140u,
               "host font record layout changed");
_Static_assert(sizeof(AstraHostMidiPreset) == 28u,
               "host MIDI preset layout changed");
_Static_assert(sizeof(AstraHostMidiStatus) == 24u,
               "host MIDI status layout changed");
/** @endcond */

/** A PCM voice's queue and error counters, returned by ASTRA_HOST_AUDIO_STATUS. */
typedef struct AstraHostAudioStatus {
    uint32_t queued_frames;    /**< Frames queued on the host, not yet played. */
    uint32_t hardware_frames;  /**< Frames the audio hardware has consumed. */
    uint32_t underruns;        /**< Playback buffer underrun count. */
    uint32_t overflows;        /**< Playback queue overflow count. */
    uint32_t software_gaps;    /**< Gaps introduced by late software submission. */
} AstraHostAudioStatus;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraHostAudioStatus) == 20u,
               "host audio status ABI changed");
/** @endcond */

/**
 * A point-in-time, read-only view of the work done below Astra's VFS. Values
 * are append-only and indexed so this wire record can grow without exposing
 * QEMU structures or host pointers. Each 64-bit value is split explicitly;
 * the ABI is identical on the MC68040, Linux, and a future hardware bridge.
 */
enum {
    ASTRA_HOST_METRIC_TIMESTAMP_NS = 0,     /**< Host wall-clock time, in nanoseconds. */
    ASTRA_HOST_METRIC_BLOCK_READ_REQUESTS,  /**< Completed block-device read requests. */
    ASTRA_HOST_METRIC_BLOCK_READ_SECTORS,   /**< Sectors read by the block device. */
    ASTRA_HOST_METRIC_BLOCK_WRITE_REQUESTS, /**< Completed block-device write requests. */
    ASTRA_HOST_METRIC_BLOCK_WRITE_SECTORS,  /**< Sectors written by the block device. */
    ASTRA_HOST_METRIC_BLOCK_FLUSH_REQUESTS, /**< Completed block-device flush requests. */
    ASTRA_HOST_METRIC_BLOCK_DURABILITY_TRANSITIONS, /**< Completed block writes/flushes, each a durability checkpoint. */
    ASTRA_HOST_METRIC_HOST_SUBMISSIONS,     /**< Host-transport batches submitted (HOST_EXECUTE calls). */
    ASTRA_HOST_METRIC_HOST_COMMANDS,        /**< Individual AstraHostCommand entries executed. */
    ASTRA_HOST_METRIC_HOST_EXECUTION_NS,    /**< Cumulative host command execution time, in nanoseconds. */
    ASTRA_HOST_METRIC_HOST_INFLIGHT,        /**< Host commands currently in flight. */
    ASTRA_HOST_METRIC_HOST_MAX_INFLIGHT,    /**< High-water mark of commands in flight. */
    ASTRA_HOST_METRIC_FS_COUNT_BASE,        /**< First of ASTRA_HOST_FS_MAX + 1 per-operation command counts. */
    ASTRA_HOST_METRIC_FS_EXECUTION_NS_BASE =
        ASTRA_HOST_METRIC_FS_COUNT_BASE + ASTRA_HOST_FS_MAX + 1u,
        /**< First of ASTRA_HOST_FS_MAX + 1 per-operation execution-time totals. */
    ASTRA_HOST_METRIC_COUNT =
        ASTRA_HOST_METRIC_FS_EXECUTION_NS_BASE +
        ASTRA_HOST_FS_MAX + 1u
        /**< Count of defined metric indices. */
};

/** Fixed byte size of ::AstraHostMetricsSnapshot for this version. */
#define ASTRA_HOST_METRICS_SNAPSHOT_SIZE \
    (16u + 8u * (uint32_t)ASTRA_HOST_METRIC_COUNT)

/** One metric's value, as a 64-bit integer split into two big-endian halves. */
typedef struct AstraHostMetricValue {
    uint32_t hi; /**< Upper 32 bits of the value. */
    uint32_t lo; /**< Lower 32 bits of the value. */
} AstraHostMetricValue;

/** Snapshot of every metric, as returned by ASTRA_HOST_METRICS_SNAPSHOT. */
typedef struct AstraHostMetricsSnapshot {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size; /**< ::ASTRA_HOST_METRICS_SNAPSHOT_SIZE. */
    uint16_t version;    /**< ::ASTRA_HOST_METRICS_VERSION. */
    uint16_t count;      /**< Number of populated entries in values. */
    uint32_t reserved[2]; /**< Reserved; zero. */
    AstraHostMetricValue values[ASTRA_HOST_METRIC_COUNT]; /**< One entry per ASTRA_HOST_METRIC_* index. */
} AstraHostMetricsSnapshot;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraHostMetricsSnapshot) ==
                   ASTRA_HOST_METRICS_SNAPSHOT_SIZE,
               "host metrics snapshot ABI changed");
/** @endcond */

/** Fixed byte size of ::AstraHostFilesystemInfo. */
#define ASTRA_HOST_FILESYSTEM_INFO_SIZE 64u
/** Fixed big-endian payload returned by ASTRA_HOST_FS_FILESYSTEM_INFO; the
 * host's statvfs(2)-equivalent view of the volume.
 */
typedef struct AstraHostFilesystemInfo {
    uint32_t size;                  /**< ::ASTRA_HOST_FILESYSTEM_INFO_SIZE. */
    uint32_t flags;                 /**< Host mount flags (statvfs(2) f_flag). */
    uint32_t block_size;            /**< Host filesystem block size, in bytes. */
    uint32_t fragment_size;         /**< Host filesystem fragment size, in bytes. */
    uint32_t blocks_hi;             /**< Total blocks, upper 32 bits. */
    uint32_t blocks_lo;             /**< Total blocks, lower 32 bits. */
    uint32_t blocks_free_hi;        /**< Free blocks, upper 32 bits. */
    uint32_t blocks_free_lo;        /**< Free blocks, lower 32 bits. */
    uint32_t blocks_available_hi;   /**< Blocks free to an unprivileged user, upper 32 bits. */
    uint32_t blocks_available_lo;   /**< Blocks free to an unprivileged user, lower 32 bits. */
    uint32_t files_hi;              /**< Total file nodes, upper 32 bits. */
    uint32_t files_lo;              /**< Total file nodes, lower 32 bits. */
    uint32_t files_free_hi;         /**< Free file nodes, upper 32 bits. */
    uint32_t files_free_lo;         /**< Free file nodes, lower 32 bits. */
    uint32_t name_max;              /**< Maximum file name length the host allows. */
    uint32_t reserved;              /**< Reserved; zero. */
} AstraHostFilesystemInfo;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraHostFilesystemInfo) ==
                   ASTRA_HOST_FILESYSTEM_INFO_SIZE,
               "host filesystem-info ABI changed");
/** @endcond */

/** Read one metric's value out of a snapshot.
 * @param snapshot Snapshot to read, or NULL.
 * @param metric ASTRA_HOST_METRIC_* index.
 * @return The metric's 64-bit value, or zero if snapshot is NULL or metric
 *         is not within the snapshot's populated count.
 */
static inline uint64_t
astra_host_metric_value(const AstraHostMetricsSnapshot *snapshot,
                        uint32_t metric)
{
    return snapshot != NULL && metric < snapshot->count ?
        ((uint64_t)snapshot->values[metric].hi << 32) |
            snapshot->values[metric].lo : 0u;
}

/** Write one metric's value into a snapshot.
 * @param snapshot Snapshot to update; ignored if NULL.
 * @param metric ASTRA_HOST_METRIC_* index; ignored if out of range.
 * @param value 64-bit value to store, split into the entry's hi/lo halves.
 */
static inline void
astra_host_metric_set(AstraHostMetricsSnapshot *snapshot, uint32_t metric,
                      uint64_t value)
{
    if (snapshot == NULL || metric >= ASTRA_HOST_METRIC_COUNT)
        return;
    snapshot->values[metric].hi = (uint32_t)(value >> 32);
    snapshot->values[metric].lo = (uint32_t)value;
}

/** Seconds since the epoch, split, and nanoseconds within the second. */
typedef struct AstraHostTime {
    int32_t seconds_hi;     /**< Signed seconds, upper 32 bits. */
    uint32_t seconds_lo;    /**< Seconds, lower 32 bits. */
    uint32_t nanoseconds;   /**< 0..999999999. */
} AstraHostTime;

/** AstraHostNode::valid bits: which fields the provider filled. A field
 * whose bit is clear is unknown, never zero-by-default. */
enum {
    ASTRA_HOST_NODE_TYPE = 1u << 0,
    ASTRA_HOST_NODE_MODE = 1u << 1,
    ASTRA_HOST_NODE_NLINK = 1u << 2,
    ASTRA_HOST_NODE_UID = 1u << 3,
    ASTRA_HOST_NODE_GID = 1u << 4,
    ASTRA_HOST_NODE_SIZE = 1u << 5,
    ASTRA_HOST_NODE_BLOCKS = 1u << 6,
    ASTRA_HOST_NODE_ATIME = 1u << 7,
    ASTRA_HOST_NODE_MTIME = 1u << 8,
    ASTRA_HOST_NODE_CTIME = 1u << 9,
    ASTRA_HOST_NODE_BTIME = 1u << 10,
    ASTRA_HOST_NODE_INODE = 1u << 11,
    ASTRA_HOST_NODE_DEVICE = 1u << 12,
    ASTRA_HOST_NODE_MOUNT = 1u << 13,
    ASTRA_HOST_NODE_BLOCK_SIZE = 1u << 14
};

/** One node's metadata, as statx(2) reports it (docs/FILESYSTEM_PRODUCTION_PRD.md FS22). */
typedef struct AstraHostNode {
    uint32_t valid;         /**< ASTRA_HOST_NODE_* bits. */
    uint16_t kind;          /**< ASTRA_VFS_KIND_*. */
    uint16_t mode;          /**< st_mode: type and permission bits. */
    uint32_t nlink;         /**< Hard links. */
    uint32_t uid;           /**< Owning user. */
    uint32_t gid;           /**< Owning group. */
    uint32_t block_size;    /**< Preferred I/O size. */
    uint32_t device_hi;     /**< Native volume identity, upper 32 bits. */
    uint32_t device_lo;     /**< Native volume identity, lower 32 bits. */
    uint32_t inode_hi;      /**< Inode number, upper 32 bits. */
    uint32_t inode_lo;      /**< Inode number, lower 32 bits. */
    uint32_t size_hi;       /**< Bytes, upper 32 bits. */
    uint32_t size_lo;       /**< Bytes, lower 32 bits. */
    uint32_t blocks_hi;     /**< 512-byte blocks allocated, upper 32 bits. */
    uint32_t blocks_lo;     /**< 512-byte blocks allocated, lower 32 bits. */
    uint32_t mount_hi;      /**< Native mount identity, upper 32 bits. */
    uint32_t mount_lo;      /**< Native mount identity, lower 32 bits. */
    AstraHostTime atime;    /**< Last access. */
    AstraHostTime mtime;    /**< Last modification. */
    AstraHostTime ctime;    /**< Last status change. */
    AstraHostTime btime;    /**< Creation: tells a reused inode number apart. */
} AstraHostNode;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraHostTime) == 12u, "host time ABI changed");
_Static_assert(sizeof(AstraHostNode) == 112u, "host node ABI changed");
/** @endcond */

/** Wire-format version of ::AstraHostCommand. */
#define ASTRA_HOST_COMMAND_VERSION 6u
/** Fixed byte size of ::AstraHostCommand. */
#define ASTRA_HOST_COMMAND_SIZE 512u

/**
 * One host command, shared by every host service (filesystem, metrics,
 * remote desktop, entropy, audio); AstraHostCommand::service and
 * AstraHostCommand::operation select which service and operation interpret
 * the rest. Paths are not fields of the record: a filesystem command's path
 * operands are length-counted bytes in the data span (path_offset), so the
 * record caps no path; data follows the command array inside the same DMA
 * buffer. Split 64-bit values keep the layout
 * identical on MC68040, Linux and a future RTL engine.
 */
typedef struct AstraHostCommand {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size; /**< ::ASTRA_HOST_COMMAND_SIZE. */
    uint16_t version;      /**< ::ASTRA_HOST_COMMAND_VERSION. */
    uint16_t service;      /**< ASTRA_HOST_SERVICE_* selecting the interpreting service. */
    uint16_t operation;    /**< Service-specific operation code (e.g. ASTRA_HOST_FS_*). */
    uint16_t flags;        /**< Operation-specific flags; zero unless the operation defines some. */
    uint32_t status;       /**< ASTRA_SYSCALL_*-style result, filled in by the host on completion. */
    uint32_t handle;       /**< Service-specific open handle (e.g. a host file handle), or zero. */
    uint32_t generation;   /**< Host generation this command targets; refused if stale. */
    uint32_t offset_hi;    /**< Operation-specific 64-bit byte offset, upper 32 bits. */
    uint32_t offset_lo;    /**< Operation-specific 64-bit byte offset, lower 32 bits. */
    uint32_t value_hi;     /**< Operation-specific 64-bit scratch value, upper 32 bits. */
    uint32_t value_lo;     /**< Operation-specific 64-bit scratch value, lower 32 bits. */
    uint32_t data_offset;  /**< Byte offset of this command's payload within the DMA buffer. */
    uint32_t data_length;  /**< Input payload bytes available at data_offset. */
    uint32_t data_capacity;/**< Maximum output bytes the host may write at data_offset. */
    uint32_t result_length;/**< Output bytes the host actually wrote. */
    uint32_t result_value; /**< Operation-specific scalar result. */
    /** Filesystem: the session the operation acts in (0 only for CLAIM
     * and a root SESSION_OPEN). Every other service: zero. */
    uint32_t session;
    /** Filesystem: DMA-buffer offset of the path operands -- path_length
     * bytes of `path` then path2_length bytes of `path2`, unterminated;
     * zero when both are empty. Every other service: zero. */
    uint32_t path_offset;
    uint16_t path_length;   /**< Bytes of the first path operand. */
    uint16_t path2_length;  /**< Bytes of the second path operand. */
    /** Filesystem: metadata answered by OPEN, STAT and READDIR. */
    AstraHostNode node;
    uint32_t reserved[82];  /**< Reserved; zero. */
} AstraHostCommand;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraHostCommand) == ASTRA_HOST_COMMAND_SIZE,
               "host command ABI changed");
/** @endcond */

/** Fixed byte size of ::AstraHostLeaseInfo. */
#define ASTRA_HOST_LEASE_INFO_SIZE 32u
/** The host device lease's capabilities and limits, returned by HOST_QUERY. */
typedef struct AstraHostLeaseInfo {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size; /**< ::ASTRA_HOST_LEASE_INFO_SIZE. */
    uint32_t capabilities;   /**< ASTRA_HOST_CAP_* bits the host supports. */
    uint32_t state_flags;    /**< ASTRA_HOST_STATE_* bits; ASTRA_HOST_STATE_READY once usable. */
    uint32_t host_generation;/**< Current host generation. */
    uint32_t maximum_transfer; /**< Largest byte_size HOST_EXECUTE or HOST_CHANNEL_OPEN accepts. */
    uint32_t maximum_commands; /**< Largest command_count or command_capacity accepted. */
    uint32_t scope;          /**< HOST_QUERY on a scope: the provider word it binds; 0 on a device. */
    uint32_t scope_flags;    /**< HOST_QUERY on a scope: ::ASTRA_HOST_SCOPE_READ_ONLY or 0. */
} AstraHostLeaseInfo;

/** Fixed byte size of ::AstraHostTransportRequest. */
#define ASTRA_HOST_TRANSPORT_REQUEST_SIZE 24u
/** A one-shot HOST_EXECUTE request: a bounded batch of ::AstraHostCommand
 * entries packed into an already-mapped DMA buffer.
 */
typedef struct AstraHostTransportRequest {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size; /**< ::ASTRA_HOST_TRANSPORT_REQUEST_SIZE. */
    uint32_t buffer;        /**< DMA area handle holding the command batch. */
    uint32_t buffer_offset; /**< Byte offset of the first command within buffer. */
    uint32_t byte_size;     /**< Total bytes spanned by the command batch. */
    uint32_t command_count; /**< Number of ::AstraHostCommand entries in the batch. */
    uint32_t reserved;      /**< Reserved; zero. */
} AstraHostTransportRequest;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraHostLeaseInfo) == ASTRA_HOST_LEASE_INFO_SIZE,
               "host lease ABI changed");
_Static_assert(sizeof(AstraHostTransportRequest) ==
                   ASTRA_HOST_TRANSPORT_REQUEST_SIZE,
               "host request ABI changed");
/** @endcond */

/*
 * Kernel-authenticated submission programmed with one MMIO doorbell.  This
 * record lives in kernel memory: user mode supplies the DMA handle, while the
 * kernel supplies the physical range and current process owner.  A software
 * host and a future FPGA engine therefore consume the same bounded batch
 * without trusting an owner or physical pointer written by an application.
 * Never constructed by user code; kept here only because the kernel and the
 * platform (QEMU or a future bridge) must share one definition.
 */
/** @cond ASTRA_INTERNAL */
#define ASTRA_HOST_SUBMISSION_VERSION 1u
#define ASTRA_HOST_SUBMISSION_SIZE 64u
typedef struct AstraHostSubmission {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint16_t version;
    uint16_t flags;
    uint32_t owner;
    uint32_t host_generation;
    uint32_t physical_buffer;
    uint32_t byte_size;
    uint32_t command_count;
    uint32_t reserved[9];
} AstraHostSubmission;

_Static_assert(sizeof(AstraHostSubmission) == ASTRA_HOST_SUBMISSION_SIZE,
               "host submission ABI changed");
/** @endcond */

/*
 * One owner-bound host channel.  Axiom authenticates and pins the backing DMA
 * buffer once; user mode then publishes batches and rings only its isolated
 * doorbell page.  Filesystem and hosted-tool messages share this transport.
 */
/** Native-big-endian `AHCH` signature identifying a valid channel header. */
#define ASTRA_HOST_CHANNEL_MAGIC UINT32_C(0x41484348) /* "AHCH" */
/** Current host channel header ABI revision. */
#define ASTRA_HOST_CHANNEL_VERSION 1u
/** Physical base address of the per-channel doorbell aperture. */
#define ASTRA_HOST_CHANNEL_PHYSICAL_BASE UINT32_C(0xffd00000)
/** Total byte size of the doorbell aperture. */
#define ASTRA_HOST_CHANNEL_APERTURE_SIZE UINT32_C(0x00100000)
/** Byte size of one channel's doorbell page. */
#define ASTRA_HOST_CHANNEL_PAGE_SIZE UINT32_C(0x00001000)
/** Number of channel doorbell pages, and so the maximum concurrently open channels. */
#define ASTRA_HOST_CHANNEL_COUNT \
    (ASTRA_HOST_CHANNEL_APERTURE_SIZE / ASTRA_HOST_CHANNEL_PAGE_SIZE)
/** Fixed byte size of ::AstraHostChannelHeader. */
#define ASTRA_HOST_CHANNEL_HEADER_SIZE 64u
/** Doorbell-page byte offset of AstraHostChannelHeader::flags. */
#define ASTRA_HOST_CHANNEL_STATE_OFFSET      0x08u
/** Doorbell-page byte offset of AstraHostChannelHeader::channel_generation. */
#define ASTRA_HOST_CHANNEL_GENERATION_OFFSET 0x0cu
/** Doorbell-page byte offset of AstraHostChannelHeader::consumer_position. */
#define ASTRA_HOST_CHANNEL_CONSUMER_OFFSET   0x10u
/** Doorbell-page byte offset of AstraHostChannelHeader::transport_status. */
#define ASTRA_HOST_CHANNEL_STATUS_OFFSET     0x14u
/** Doorbell-page register the runtime writes to ring the channel after publishing a command. */
#define ASTRA_HOST_CHANNEL_KICK_OFFSET       0x20u
/** Doorbell-page register that arms the channel's completion interrupt. */
#define ASTRA_HOST_CHANNEL_ARM_OFFSET        0x24u
/** Doorbell-page register that disarms the channel's completion interrupt. */
#define ASTRA_HOST_CHANNEL_DISARM_OFFSET     0x28u
/** Shared header of one host channel's doorbell page, mapped into the
 * owning process and read/written by both it and the host.
 */
typedef struct AstraHostChannelHeader {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t magic; /**< ::ASTRA_HOST_CHANNEL_MAGIC. */
    uint16_t version;      /**< ::ASTRA_HOST_CHANNEL_VERSION. */
    uint16_t header_size;  /**< ::ASTRA_HOST_CHANNEL_HEADER_SIZE. */
    uint32_t flags;        /**< Reserved; zero. */
    uint32_t command_size; /**< ::ASTRA_HOST_COMMAND_SIZE. */
    uint32_t command_capacity; /**< Power-of-two count of command slots following this header. */
    uint32_t command_offset;   /**< Byte offset of the first command slot (the header size). */
    uint32_t data_offset;      /**< Byte offset of the data span following the command slots. */
    uint32_t total_size;       /**< Total mapped byte size of the channel's DMA buffer. */
    uint32_t channel_generation; /**< Generation assigned at HOST_CHANNEL_OPEN. */
    volatile uint32_t producer_position; /**< Monotonic command count published by the owner. */
    uint32_t reserved0[2];     /**< Reserved for producer-side growth; zero. */
    volatile uint32_t consumer_position; /**< Monotonic command count consumed by the host. */
    volatile uint32_t transport_status;  /**< ASTRA_SYSCALL_*-style transport status. */
    uint32_t reserved1[2];     /**< Reserved for consumer-side growth; zero. */
} AstraHostChannelHeader;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraHostChannelHeader) ==
                   ASTRA_HOST_CHANNEL_HEADER_SIZE,
               "host channel header ABI changed");
/** @endcond */

/** Fixed byte size of ::AstraHostChannelOpen. */
#define ASTRA_HOST_CHANNEL_OPEN_SIZE 48u
/** HOST_CHANNEL_OPEN's in/out parameter: the caller fills buffer, byte_size
 * and command_capacity and zeroes the rest; the kernel fills
 * channel_generation, channel_address and host_generation on success.
 */
typedef struct AstraHostChannelOpen {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size; /**< ::ASTRA_HOST_CHANNEL_OPEN_SIZE. */
    uint32_t flags;        /**< Reserved; must be zero. */
    uint32_t buffer;       /**< DMA area handle backing the channel. */
    uint32_t byte_size;    /**< Bytes of buffer to dedicate to the channel. */
    uint32_t command_capacity; /**< Power-of-two command slot count to request. */
    uint32_t channel_generation; /**< Zero on input; the assigned generation on output. */
    uint32_t channel_address;    /**< Zero on input; the mapped doorbell virtual address on output. */
    uint32_t host_generation;    /**< Zero on input; the host generation at open on output. */
    uint32_t reserved[4];  /**< Reserved; must be zero. */
} AstraHostChannelOpen;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraHostChannelOpen) == ASTRA_HOST_CHANNEL_OPEN_SIZE,
               "host channel open ABI changed");
/** @endcond */

/*
 * Kernel-to-platform channel lifecycle control, built and consumed entirely
 * inside the kernel's platform layer; never constructed by user code.
 */
/** @cond ASTRA_INTERNAL */
/* Version 5: OPEN carries the native PID separately from resource ownership. */
#define ASTRA_HOST_CHANNEL_CONFIG_VERSION 5u
#define ASTRA_HOST_CHANNEL_CONFIG_SIZE 68u
#define ASTRA_HOST_CHANNEL_CONFIG_OPEN  1u
#define ASTRA_HOST_CHANNEL_CONFIG_CLOSE 2u
/** Bind one more scope to an open bound channel. */
#define ASTRA_HOST_CHANNEL_CONFIG_ATTACH 3u
/** Unbind a scope (revocation): a command naming it that has not started
 * is refused from the moment this configuration write returns. */
#define ASTRA_HOST_CHANNEL_CONFIG_DETACH 4u
/** scope_flags of an unbound OPEN: the opener's device handle carries
 * ASTRA_RIGHT_HOST_SCOPE (the filesystem service's grant). Only such a
 * channel may claim the filesystem (ASTRA_HOST_FS_CLAIM): the kernel, not
 * the command, says who the provider is. */
#define ASTRA_HOST_CHANNEL_PROVIDER (1u << 3)
typedef struct AstraHostChannelConfig {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint16_t version;
    uint16_t operation;
    uint32_t slot;
    uint32_t owner;
    uint32_t host_generation;
    uint32_t channel_generation;
    uint32_t physical_buffer;
    uint32_t byte_size;
    uint32_t command_capacity;
    uint32_t scope;        /**< OPEN (bound), ATTACH, DETACH: the scope's provider word. */
    uint32_t scope_flags;  /**< ASTRA_HOST_SCOPE_BOUND | READ_ONLY; an unbound OPEN 0 or ASTRA_HOST_CHANNEL_PROVIDER; 0 for CLOSE and DETACH. */
    uint32_t scope_owner;  /**< OPEN (bound), ATTACH: owner of the process that created the scope. */
    uint32_t scope_files;  /**< Per-owner handles and shares in this scope; zero for unlimited. */
    uint32_t process;      /**< OPEN: kernel-authenticated native PID, independent of owner; zero for other operations. */
    uint32_t reserved[3];  /**< Zero. */
} AstraHostChannelConfig;

_Static_assert(sizeof(AstraHostChannelConfig) ==
                   ASTRA_HOST_CHANNEL_CONFIG_SIZE,
               "host channel config ABI changed");
/** @endcond */

/** Reusable one-command client for low-volume host services.
 *
 * The runtime owns channel setup, DMA lifetime, publication fences, and
 * completion validation. High-throughput providers such as the VFS retain
 * their per-thread, multi-command lanes instead of serializing through this
 * single-command convenience client.
 */
typedef struct AstraHostChannelClient {
    uint32_t device;          /**< Borrowed host-device capability. */
    uint32_t dma;             /**< Runtime-owned DMA area handle. */
    uint32_t channel_address; /**< Kernel-authenticated doorbell page. */
    uint32_t generation;      /**< Host generation captured at open. */
    uint32_t producer;        /**< Monotonic command sequence. */
    uint32_t byte_size;       /**< Mapped DMA span. */
    uint32_t data_capacity;   /**< Bytes following the command slot. */
    volatile AstraHostChannelHeader *header; /**< Shared channel header. */
    volatile AstraHostCommand *command;      /**< Sole command slot. */
    volatile uint8_t *data;                  /**< Command data span. */
} AstraHostChannelClient;

/** Open a reusable one-command host channel.
 * @param device Host-device capability.
 * @param required_capabilities ASTRA_HOST_CAP_* bits the caller requires.
 * @param data_capacity Bytes required after the command slot.
 * @param client Zeroed client receiving the opened channel.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_host_client_open(uint32_t device,
                                uint32_t required_capabilities,
                                uint32_t data_capacity,
                                AstraHostChannelClient *client);
/** Reset and initialize the client's command slot.
 * @param client Opened client whose command slot is reset.
 * @param service ASTRA_HOST_SERVICE_* to stamp into the command.
 * @param operation Service-specific operation code to stamp into the command.
 * @return Writable command slot, or NULL for an invalid client.
 */
AstraHostCommand *astra_host_client_prepare(AstraHostChannelClient *client,
                                            uint16_t service,
                                            uint16_t operation);
/** Publish the prepared command and wait for its completion.
 * @param client Client whose prepared command slot is published.
 * @return ASTRA_SYSCALL_* transport status; command status remains in slot.
 */
uint32_t astra_host_client_submit(AstraHostChannelClient *client);
/** Close the channel and its DMA area, leaving the client zeroed.
 * @param client Client to close.
 * @return First ASTRA_SYSCALL_* close failure, or ASTRA_SYSCALL_OK.
 */
uint32_t astra_host_client_close(AstraHostChannelClient *client);

#endif
