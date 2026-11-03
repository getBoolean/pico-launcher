#pragma once
struct rtos_event_t { };
inline void rtos_createEvent(rtos_event_t*) { }
inline void rtos_signalEvent(rtos_event_t*) { }
inline void rtos_waitEvent(rtos_event_t*, bool, bool) { }
