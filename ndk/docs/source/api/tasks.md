# Independent work

Use `astra_rt_thread_start_detached()` for finite work in the current process
that does not need a result or cancellation. It copies the entry and argument,
starts the thread, and releases the caller's handle. The kernel reaps the
thread and its stack when it exits. The argument must remain valid until the
entry has consumed it; pass an integer or separately owned state, not a
pointer to a returning function's stack frame.

```c
static void work(uint32_t argument)
{
    /* Do finite work, then return. */
    (void)argument;
}

AstraThreadStart start = { work, 0u };
uint32_t status = astra_rt_thread_start_detached(
    &start, ASTRA_PROCESS_PRIORITY_NORMAL);
```

If the caller needs the completion status, use `astra_rt_thread_create()` and
retain its handle instead. `astra_application_launch()` already starts a
separate application process and returns after startup succeeds without
waiting for exit; use `astra_application_launch_waitable()` when the caller
does need to wait. These use the native process and thread substrate, with no
separate task scheduler.
