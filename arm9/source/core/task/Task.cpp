#include "common.h"
#include "Task.h"

void TaskBase::RequestCancel()
{
    u32 irqs = rtos_disableIrqs();
    _cancelRequested = true;
    if (_state == TaskState::NotStarted)
    {
        _state = TaskState::Canceled;
        rtos_wakeupQueue(&_threadQueue);
    }
    rtos_restoreIrqs(irqs);
}

bool TaskBase::Execute(u32 irqs)
{
    if (_state == TaskState::NotStarted)
    {
        _state = TaskState::Running;
        rtos_restoreIrqs(irqs);
        TaskState finalState = ExecuteDirect();
        return SetFinalState(finalState);
    }
    else
    {
        bool destroyWhenComplete = _destroyWhenComplete;
        rtos_restoreIrqs(irqs);
        return destroyWhenComplete;
    }
}

bool TaskBase::SetFinalState(TaskState finalState)
{
    u32 irqs = rtos_disableIrqs();
    bool destroyWhenComplete = _destroyWhenComplete;
    if (_state == TaskState::Running)
    {
        _state = finalState;
        rtos_wakeupQueue(&_threadQueue);
    }
    rtos_restoreIrqs(irqs);
    return destroyWhenComplete;
}

bool TaskBase::Wait()
{
    u32 irq = rtos_disableIrqs();

    if (!IsCompleted())
        rtos_queueThread(rtos_getCurThread(), &_threadQueue);

    rtos_restoreIrqs(irq);

    return IsCompletedSuccessfully();
}
