# Task queue regression checks

Run `bash tests/taskQueue/run.sh` from Linux or WSL with g++ installed.

The test compiles the production task and queue code with small RTOS stubs. It
covers cancelling the only queued entry, pool-slot reuse, cancellation with
neighbours, detached ownership, teardown of completed or invalid handles, and
immediate disposal and pool-slot reuse during completion wakeup.
AddressSanitizer checks memory accesses. The host compiler warns about the
production code's 32-bit pointer casts on 64-bit hosts.

This does not simulate RTOS scheduling, interrupts, running-task waits, SD access,
or graphics. Those paths still need Nintendo DS hardware or an emulator.
