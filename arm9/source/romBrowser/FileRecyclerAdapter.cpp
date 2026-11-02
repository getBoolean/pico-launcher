#include "common.h"
#include "core/task/TaskQueue.h"
#include "FileInfoManager.h"
#include "FileRecyclerAdapter.h"

u32 FileRecyclerAdapter::GetItemCount() const
{
    return _fileInfoManager->GetItemCount();
}

void FileRecyclerAdapter::BindView(SharedPtr<View> view, int index) const
{
    LOG_DEBUG("Binding %d\n", index);
    auto self = const_cast<FileRecyclerAdapter*>(this)->SharedFromThis();
    auto queueTask = _taskQueue->Enqueue([self, view, index] (const vu8& cancelRequested)
    {
        if (cancelRequested)
        {
            LOG_DEBUG("Task to load %d was canceled\n", index);
            return TaskResult<void>::Canceled();
        }

        LOG_DEBUG("Started task to load %d\n", index);
        self->_fileInfoManager->LoadFileInfo(index);
        auto internalFileInfo = self->_fileInfoManager->GetInternalFileInfo(index);
        if (cancelRequested)
        {
            self->_fileInfoManager->ReleaseFileInfo(index);
            return TaskResult<void>::Canceled();
        }
        return self->BindView(view, index, internalFileInfo, cancelRequested);
    });
    SetQueueTask(view, std::move(queueTask));
}
