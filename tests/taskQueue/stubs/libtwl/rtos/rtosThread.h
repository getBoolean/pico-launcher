#pragma once
#include "common.h"
#include <cassert>
#include <functional>
struct rtos_thread_t { };
struct rtos_thread_queue_t { void* head; };
inline void rtos_createThread(rtos_thread_t*, u8, void (*)(void*), void*, u32*, u32) { }
inline void rtos_wakeupThread(rtos_thread_t*) { }
inline void rtos_joinThread(rtos_thread_t*) { }
inline std::function<void()> onWakeupQueue;
inline void rtos_wakeupQueue(rtos_thread_queue_t*)
{
    auto callback = std::move(onWakeupQueue);
    if (callback)
        callback();
}
inline rtos_thread_t* rtos_getCurThread() { return nullptr; }
inline void rtos_queueThread(rtos_thread_t*, rtos_thread_queue_t*)
{
    // These tests execute queued tasks synchronously, never simulate a running thread.
    assert(false && "unexpected thread wait");
}
