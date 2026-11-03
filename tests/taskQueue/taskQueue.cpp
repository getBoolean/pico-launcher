#include <cassert>
#include <cstdio>
#include "core/task/TaskQueue.h"

class TestQueue : public TaskQueue<4, sizeof(TaskBase) + 64>
{
public:
    u32 Count() const { return _taskList.GetCount(); }

    void RunOne()
    {
        auto task = _taskList.GetHead();
        assert(task);
        _taskList.Remove(task);
        if (task->Execute(0))
            ReturnOwnership(task);
    }
};

int main()
{
    TestQueue queue;
    int executed = 0;
    auto enqueue = [&] ()
    {
        return queue.Enqueue([&executed] (const vu8&)
        {
            executed++;
            return TaskResult<void>::Completed();
        });
    };

    // Cancel the sole entry, then reuse its pool slot: no dangling list head or cycle.
    auto only = enqueue();
    only.CancelTask();
    assert(!only.IsValid());
    assert(queue.Count() == 0);
    auto replacement = enqueue();
    queue.RunOne();
    replacement.Dispose();
    assert(executed == 1 && queue.Count() == 0);

    // Cancelling either end preserves its neighbour.
    auto first = enqueue();
    auto second = enqueue();
    first.CancelAndWait();
    assert(queue.Count() == 1);
    queue.RunOne();
    second.Dispose();
    assert(executed == 2 && queue.Count() == 0);
    first = enqueue();
    second = enqueue();
    second.CancelTask();
    queue.RunOne();
    first.Dispose();
    assert(executed == 3 && queue.Count() == 0);

    // Disposal can transfer ownership to the queue until execution finishes.
    auto detached = enqueue();
    detached.Dispose();
    queue.RunOne();
    assert(executed == 4 && queue.Count() == 0);

    // The teardown path is harmless for an invalid or already completed handle.
    detached.CancelAndWait();
    auto completed = enqueue();
    queue.RunOne();
    completed.CancelAndWait();
    assert(!completed.IsValid());
    assert(executed == 5 && queue.Count() == 0);

    // RTOS wakeup can immediately resume the owner, destroying and reusing the
    // just-completed task before the worker returns from Execute.
    auto waking = enqueue();
    QueueTask<void> reused;
    onWakeupQueue = [&]
    {
        waking.Dispose();
        reused = enqueue();
    };
    queue.RunOne();
    assert(queue.Count() == 1 && executed == 6);
    queue.RunOne();
    reused.Dispose();
    assert(queue.Count() == 0 && executed == 7);
    std::puts("Task queue regression checks passed");
}
