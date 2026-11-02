#pragma once
#include "common.h"
inline u32 rtos_disableIrqs() { return 0; }
inline void rtos_restoreIrqs(u32) { }
