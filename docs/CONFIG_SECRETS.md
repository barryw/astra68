# Config Kit protected credentials

Config Kit 1.2 adds `astra_config_set_secret` and
`astra_config_get_secret`. A caller must already hold a scoped `CONFIG` or
`CONFIG_SYSTEM` grant and the resident-only `HOST_SECRETS` grant. Each value is stored
as an authenticated `ASC1` envelope under `secret.<key>` in that owner's
existing `settings.conf`; the Config Kit's existing atomic document commit
handles persistence. The API accepts 1–1024 bytes. The envelope uses
XChaCha20-Poly1305 with a fresh 24-byte host nonce and binds both the
capability root and full setting name as associated data. A changed envelope,
key, root, or setting name returns `ASTRA_CONFIG_AUTH_FAILED`.
Ordinary apps, including Settings, cannot request that host right. A future
network mount service will own saved share credentials and accept narrowly
scoped requests from Settings over its native service port.

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
and access-control boundary. Their flat-buffer encrypt/decrypt functions are
unfinished placeholders; Astra uses the audited Monocypher 4.0.3 AEAD
implementation instead. Config Kit retains Astra's scoped capability model
and atomic document commit.

Haiku's MC68040 VM mapping in
`src/system/kernel/arch/m68k/paging/040/M68KVMTranslationMap040.cpp`
invalidates translations when pages are unmapped. Astra's shared host-channel
mapping uses a distinct page per live channel and retires each mapping on
close. This lets Config Kit's storage and secret channels coexist in one
thread without bypassing the normal kernel ownership check.
