#include "common.h"
#include <libtwl/rtos/rtosIrq.h>
#include "WeakPtr.h"

void WeakPtrBase::ResetIntern()
{
    u32 irq = rtos_disableIrqs();
    auto refCount = _refCount;
    _refCount = nullptr;
    if (--refCount->weakRefCount == 0 && refCount->refCount == 0)
    {
        rtos_restoreIrqs(irq);
        delete refCount;
    }
    else
    {
        rtos_restoreIrqs(irq);
    }
}

bool WeakPtrBase::LockIntern() const
{
    u32 irq = rtos_disableIrqs();
    if (_refCount->refCount != 0)
    {
        _refCount->refCount++;
        rtos_restoreIrqs(irq);
        return true;
    }
    else
    {
        rtos_restoreIrqs(irq);
        return false;
    }
}
