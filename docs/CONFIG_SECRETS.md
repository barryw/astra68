# Config Kit protected credentials

DE25 selected release `01ee57e38814f4710ecb9af68c41af14a685f6593798b1666331e2d95521d985`
includes this facility. Production host key was generated on first start;
private certification covered seal/open, restart, and ordinary-channel denial.
See `docs/CURRENT_STATE.md` for release evidence.

Config Kit 1.2 added `astra_config_set_secret` and
`astra_config_get_secret`. A caller must already hold a scoped `CONFIG` or
`CONFIG_SYSTEM` grant and the resident-only `HOST_SECRETS` grant. Each value is stored
as an authenticated `ASC1` envelope under `secret.<key>` in that owner's
existing `settings.conf`; the Config Kit's existing atomic document commit
handles persistence. The API accepts 1–1024 bytes. The envelope uses
XChaCha20-Poly1305 with a fresh 24-byte host nonce and binds both the
capability root and full setting name as associated data. A changed envelope,
key, root, or setting name returns `ASTRA_CONFIG_AUTH_FAILED`.
Ordinary apps, including Settings, cannot request that host right. Config Kit
1.3 adds a resident, generic `credentials` service as the only owner of its
protected document. `CRED_WRITE` permits set/remove; `CRED_READ`
permits get. Settings receives only the write capability. Production grants
the read capability to no app or service yet; a future trusted consumer must
receive it explicitly. The public `astra_credential_set/get/remove` functions
in `config.library.1` hide the service protocol and shared-area transfer.
References are bounded names, not paths, and values retain Config Kit's
1–1024 byte limit. The service copies incoming bytes before sealing so a
sender cannot mutate a shared area during encryption. Client and service
clear their private plaintext buffers. A successful read transfers one area
to its authorized caller, which copies and clears it. No credential value is
logged or placed in startup arguments, environment, or the public volume
catalog.

This is a system credential store: only trusted Settings may write its key
namespace. Granting `CRED_WRITE` to arbitrary applications would let one
app replace another's reference. Add per-owner namespaces before making that
grant available to untrusted applications. The current network settings work
does not need such a grant.

The root storage launcher generates 32 random key bytes on first use in
`$ASTRA_STATE_ROOT/config-secret.key`. It creates a temporary root-owned
0600 file within a root-owned, non-writable state directory, syncs it, and
publishes it without replacing an existing key. It passes an open descriptor
to the unprivileged QEMU child, which reads the key once and closes the
descriptor. The key never enters Astra's guest filesystem, startup manifest,
command line, or log. A missing or malformed key must never lead to
replacing existing ciphertext. Back up this key with the host state: loss of
the key makes saved credentials unreadable. Do not restore ciphertext without
its matching key.

This protects guest storage and keeps credentials out of ordinary settings.
It does **not** protect against theft of the entire host SD card: both host
key and encrypted guest image live on that card. User chose automatic unlock
without TPM or passphrase. Add a hardware-backed or user-unlocked key when
protection against offline host-media access is required.

Haiku reference: `src/servers/keystore/Keyring.cpp` and
`src/servers/keystore/KeyStoreServer.cpp` establish a service-owned keyring
and access-control boundary. `_ResolveCallingApp` and `_ValidateAppAccess`
enforce caller permissions there; Astra adapts this to separate native port
capabilities because its port messages carry no caller identity. Haiku's
flat-buffer encrypt/decrypt functions are
unfinished placeholders; Astra uses the audited Monocypher 4.0.3 AEAD
implementation instead. Config Kit retains Astra's scoped capability model
and atomic document commit.

Haiku's MC68040 VM mapping in
`src/system/kernel/arch/m68k/paging/040/M68KVMTranslationMap040.cpp`
invalidates translations when pages are unmapped. Astra's shared host-channel
mapping uses a distinct page per live channel and retires each mapping on
close. This lets Config Kit's storage and secret channels coexist in one
thread without bypassing the normal kernel ownership check.
