#include <astra/input_library.h>

#include <assert.h>

extern const AstraInputLibraryV1 astra_library_exports;

AstraResult astra_pointer_observer_open(AstraHandle service,
                                        uint32_t subscriptions,
                                        AstraPointerObserver *observer)
{
    (void)service;
    (void)subscriptions;
    (void)observer;
    return ASTRA_OK;
}

AstraResult astra_pointer_event_try(AstraPointerObserver *observer,
                                    AstraPointerEvent *event)
{
    (void)observer;
    (void)event;
    return ASTRA_OK;
}

AstraResult astra_pointer_event_wait(AstraPointerObserver *observer,
                                     AstraPointerEvent *event,
                                     AstraMonotonicDeadline deadline)
{
    (void)observer;
    (void)event;
    (void)deadline;
    return ASTRA_OK;
}

AstraResult astra_pointer_observer_close(AstraPointerObserver *observer)
{
    (void)observer;
    return ASTRA_OK;
}

int main(void)
{
    const AstraInputLibraryV1 *library = &astra_library_exports;

    assert(library->abi_major == ASTRA_INPUT_LIBRARY_ABI_MAJOR);
    assert(library->abi_minor == ASTRA_INPUT_LIBRARY_ABI_MINOR);
    assert(library->structure_size == sizeof(*library));
    assert(library->pointer_observer_open == astra_pointer_observer_open);
    assert(library->pointer_event_try == astra_pointer_event_try);
    assert(library->pointer_event_wait == astra_pointer_event_wait);
    assert(library->pointer_observer_close == astra_pointer_observer_close);
    return 0;
}
