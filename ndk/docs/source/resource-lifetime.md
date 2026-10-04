# Resource Lifetime

Astra is a multitasking system with protected processes. Every kernel object
and service a program uses -- areas, ports, rings, windows, surfaces --
is reached through a process-owned handle, usually inside a typed NDK
wrapper. Programs never touch hardware registers.

## Handles and typed wrappers

{c:type}`AstraHandle` is an opaque capability token. A typed wrapper such as
{c:struct}`AstraArea` or {c:struct}`AstraPort` restricts which operations can
consume that token and prevents unrelated handles from being mixed
accidentally.

A live typed wrapper is **move-only by convention in C**: initialize it once,
do not copy it, and release or transfer it exactly once. Copying a live wrapper
can create two apparent owners for one underlying resource.

Use {c:func}`astra_handle_close` for a raw capability that has no narrower
typed close operation. A successful close invalidates the caller's variable.
Copying the numeric value does not create another kernel reference; every copy
becomes stale when the capability is closed or moved through a message.

## Deterministic cleanup

GCC and Clang builds may use a typed `ASTRA_AUTO_*` declaration to release a
resource when normal control flow leaves its scope. Explicit release remains
available when the application must handle cleanup errors or end ownership
early.

Automatic cleanup is not the crash-recovery mechanism. The operating system
owns the process handle table and revokes every remaining capability when a
process exits, crashes, or is terminated.

{c:struct}`AstraPort` supports {c:macro}`ASTRA_AUTO_PORT` for a locally owned
receive/send pair. Successful message transfer invalidates each moved handle in
the supplied array, so later scope cleanup cannot accidentally close authority
that now belongs to the receiver.

## Blocking and asynchronous work

Each operation documents whether it can block. Operations that wait take an
absolute {c:type}`AstraMonotonicDeadline`. Asynchronous graphics work returns
an {c:struct}`AstraFence`; the objects it references stay valid until the
fence signals, whatever the program closes meanwhile.
