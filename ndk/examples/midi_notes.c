#include <astra/midi.h>
#include <astra/program.h>
#include <astra/runtime.h>
#include <astra/stream.h>

ASTRA_PROGRAM("midi_notes", 1, 0, 0, "Your Name", "Copyright 2026 Your Name");

/* A C major arpeggio on the host's wavetable synthesizer: the MC68040 sends
   a few bytes per note and the host renders every sample. */
enum { CHANNEL = 0u, PIANO = 0u, VELOCITY = 100u, NOTE_NS = 150000000u };

static const uint8_t notes[] = {60u, 64u, 67u, 72u};

/* A sleep that reaches its deadline ends TIMED_OUT: that is success. */
static AstraResult sleep_ns(uint64_t nanoseconds)
{
    uint32_t status = astra_rt_thread_sleep(
        nanoseconds, ASTRA_THREAD_SLEEP_RELATIVE, 0u, 0);

    return status == ASTRA_SYSCALL_TIMED_OUT ?
        ASTRA_OK : astra_result_from_syscall(status);
}

/* The step that failed, for the message. */
static const char *step = "open";

static AstraResult play(AstraHandle service)
{
    AstraMidiSynth synth = ASTRA_MIDI_SYNTH_INIT;
    const AstraMidiEvent program = {(uint8_t)(0xc0u | CHANNEL), PIANO, 0u,
                                    0u};
    AstraResult result = astra_midi_open(service, &synth);
    AstraResult closed;
    int active = 1;

    if (result != ASTRA_OK)
        return result;
    step = "program change";
    result = astra_midi_send(&synth, &program, 1u);
    for (uint32_t index = 0u;
         index < sizeof(notes) && result == ASTRA_OK; ++index) {
        step = "note on";
        result = astra_midi_note_on(&synth, CHANNEL, notes[index], VELOCITY);
        if (result == ASTRA_OK) {
            step = "sleep";
            result = sleep_ns(NOTE_NS);
        }
        if (result == ASTRA_OK) {
            step = "note off";
            result = astra_midi_note_off(&synth, CHANNEL, notes[index]);
        }
    }
    /* Let the last note's release ring out before closing. */
    for (uint32_t tries = 0u; result == ASTRA_OK && active && tries < 40u;
         ++tries) {
        step = "active";
        result = astra_midi_active(&synth, &active);
        if (result == ASTRA_OK && active)
            result = sleep_ns(NOTE_NS / 3u);
    }
    closed = astra_midi_close(&synth);
    if (result == ASTRA_OK && closed != ASTRA_OK)
        step = "close";
    return result == ASTRA_OK ? closed : result;
}

int astra_main(const AstraStartupInfo *startup)
{
    const AstraStartupCapability *output =
        astra_startup_capability(startup, "STDOUT");
    const AstraStartupCapability *pcm =
        astra_startup_capability(startup, ASTRA_CAPABILITY_PCM);

    if (output == 0)
        return 1;
    if (pcm == 0) {
        (void)astra_print(output->handle, "midi_notes: no PCM capability\n");
        return 2;
    }
    if (play(pcm->handle) != ASTRA_OK) {
        (void)astra_print(output->handle, "midi_notes: ");
        (void)astra_print(output->handle, step);
        (void)astra_print(output->handle, " failed\n");
        return 3;
    }
    (void)astra_print(output->handle,
                      "midi_notes: C E G C on the host's wavetable\n");
    return 0;
}
