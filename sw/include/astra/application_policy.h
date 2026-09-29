#ifndef ASTRA_APPLICATION_POLICY_H
#define ASTRA_APPLICATION_POLICY_H

/*
 * The launch ceiling of an application that the startup manifest does not
 * name. An application launched through APP_LAUNCH receives only grants its
 * bundle declares, and every declared grant must lie inside this ceiling or
 * inside the ceiling of a `trusted` startup manifest line for that bundle.
 * Device, interrupt, clock, service-manager and event-control authority stay
 * with the startup manifest's system entries.
 *
 * X(name, rights): rights is RAW for a capability, R or RW for the widest
 * namespace access allowed. emu/qemu/astra_image.py parses these lines to
 * check every installed bundle, so keep one entry per line.
 */
#define ASTRA_APPLICATION_CEILING(X) \
    X("GUI", RAW) \
    X("CLIPBOARD", RAW) \
    X("PCM", RAW) \
    X("NETWORK", RAW) \
    X("NETWORK_LISTEN", RAW) \
    X("NTP", RAW) \
    X("ENTROPY", RAW) \
    X("POSIX_PROCESS", RAW) \
    X("APP_LAUNCH", RAW) \
    X("APPS", R) \
    X("LIBS", R) \
    X("STORE", RW) \
    X("CONFIG", RW) \
    X("HOME", RW) \
    X("WORK", RW) \
    X("TMP", RW) \
    X("RAM", RW)

#endif
