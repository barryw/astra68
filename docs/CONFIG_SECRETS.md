# Config Kit protected settings

Config Kit 2.0 uses each owner's private settings document. Its static schema
lists every allowed key; `ASTRA_CONFIG_FIELD_SENSITIVE` marks values that
Config Kit seals and opens through ordinary typed get/set calls. Unknown keys
are refused. The old global `credentials` service, `CRED_*` grants, and
`astra_credential_*`/`astra_config_*_secret` calls are retired. No application
gets the host secret channel.

The root storage provider keeps documents in root-owned host state rather than
the broadly readable DH0 image. Before QEMU starts, it migrates the former
`/config` tree without discarding existing files, metadata, or xattrs. HostFS
publishes this state only through provider-rooted `CONFIG` grants. Supervisor
derives owner directories for applications and services. Command directory
provisioning exists, but Terminal still carries zsh's grant through `execve`;
per-command isolation remains a release gate. Public
system settings live in separate canonical documents under `/config/public`,
with a read-only reader grant and per-service writer grant. Publishing never
copies a private document. Public schemas reject sensitive fields.

HostFS alone holds `HOST_SECRETS`. Config Kit sends a sensitive key and value
through its existing `CONFIG` grant port. HostFS supplies its canonical
provider root as authenticated context, then calls the host-held
XChaCha20-Poly1305 service. The caller cannot name another owner in the
context. Each seal uses a fresh 24-byte nonce. Ciphertext remains an `ASC1`
envelope encoded in the owner's atomically replaced `settings.conf`. A
changed envelope, key, root, or host key fails authentication. Temporary
plaintext in the host channel and Config Kit transfer area is wiped. Values
are bounded to 1–1024 bytes.

The DE25 root storage launcher generates 32 random key bytes on first use in
`$ASTRA_STATE_ROOT/config-secret.key`. It creates a temporary root-owned 0600
file within a root-owned non-writable state directory, syncs it, and publishes
it without replacing an existing key. An existing missing or malformed key
fails closed. QEMU reads the passed descriptor once and closes it. Key bytes
never enter guest files, startup manifests, command lines, or logs. Back up
host state, image, and key as one set; losing the key makes saved protected
values unreadable. Automatic unlock does not protect against theft of the
entire host SD card because key and ciphertext are stored on that card.

The selected DE25 release
`01ee57e38814f4710ecb9af68c41af14a685f6593798b1666331e2d95521d985`
has the earlier host-key service and Config Kit 1.3 credential broker. Config
Kit 2.0, private host storage, and grant-bound crypt passed disposable QEMU
and DE25 first-write/restart-read gates as staged release
`1adb46e6cb419f51c2423c006cd9b8358f61150dae741c0a0ced8dff46a608ed`.
Production has not switched to this release. Retired `secret.<key>` records stay
in migrated owner documents for data preservation; they are not silently
interpreted by 2.0 schemas. There are no production network-share credential
consumers yet.

Haiku references reviewed read-only: `src/system/kernel/fs/vfs.cpp` for
server-enforced path authority; `src/servers/keystore/KeyStoreServer.cpp` and
`Keyring.cpp` for service-owned secrets and caller checks; and
`src/system/kernel/arch/m68k/paging/040/M68KVMTranslationMap040.cpp` for
mapping retirement. Astra adapts those boundaries to native scoped grants,
HostFS provider roots, and existing host-channel ownership checks.
