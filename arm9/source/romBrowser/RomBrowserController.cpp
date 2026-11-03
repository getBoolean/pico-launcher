#include "common.h"
#include <array>
#include <string.h>
#include "fat/CurrentDirectoryPath.h"
#include "picoLoaderBootstrap.h"
#include "PicoLoaderProcess.h"
#include "settings/SettingsProcess.h"
#include "FileType/ExtensionFileTypeProvider.h"
#include "FileType/FileType.h"
#include "SdFolderFactory.h"
#include "RecursiveSdFolderFactory.h"
#include "services/settings/IAppSettingsService.h"
#include "cheats/UsrCheatRepositoryFactory.h"
#include "cheats/EmptyCheatRepository.h"
#include "cheats/PicoLoaderCheatDataFactory.h"
#include "RomBrowserController.h"

RomBrowserController::RomBrowserController(
    IAppSettingsService* appSettingsService, TaskQueueBase* ioTaskQueue,
    TaskQueueBase* bgTaskQueue)
    : _appSettingsService(appSettingsService)
    , _ioTaskQueue(ioTaskQueue), _bgTaskQueue(bgTaskQueue)
    , _fileTypeProvider(appSettingsService->GetAppSettings()) { }

RomBrowserController::~RomBrowserController()
{
    CancelSearchTask();
}

void RomBrowserController::NavigateToPath(const TCHAR* name)
{
    StringUtil::Copy(_navigatePath, name, sizeof(_navigatePath) / sizeof(_navigatePath[0]));
    _stateMachine.Fire(RomBrowserStateTrigger::Navigate);
}

void RomBrowserController::LaunchFile(const FileInfo& fileInfo)
{
    _triggerFileInfo = FileInfo(fileInfo);
    _stateMachine.Fire(RomBrowserStateTrigger::Launch);
}

void RomBrowserController::ShowGameInfo(const FileInfo& fileInfo)
{
    CancelSearchTask();
    _triggerFileInfo = FileInfo(fileInfo);
    _stateMachine.Fire(RomBrowserStateTrigger::ShowGameInfo);
}

void RomBrowserController::HideGameInfo()
{
    _stateMachine.Fire(RomBrowserStateTrigger::HideGameInfo);
}

void RomBrowserController::ShowDisplaySettings()
{
    _stateMachine.Fire(RomBrowserStateTrigger::ShowDisplaySettings);
}

void RomBrowserController::HideDisplaySettings()
{
    if (_saveSettingsPending)
    {
        _saveSettingsPending = false;
        _ioTaskQueue->Enqueue([this] (const vu8& cancelRequested)
        {
            _appSettingsService->Save();
            return TaskResult<void>::Completed();
        });
    }
    _stateMachine.Fire(RomBrowserStateTrigger::HideDisplaySettings);
}

void RomBrowserController::GotoSettingsScreen()
{
    _stateMachine.Fire(RomBrowserStateTrigger::GotoSettingsScreen);
}

void RomBrowserController::ShowSearch()
{
    _stateMachine.Fire(RomBrowserStateTrigger::ShowSearch);
}

void RomBrowserController::HideSearch()
{
    _stateMachine.Fire(RomBrowserStateTrigger::HideSearch);
}

void RomBrowserController::ApplySearch(const char* query, bool includeSubFolders)
{
    CancelSearchTask();

    if (!query[0])
    {
        _searchIncludeSubFolders = includeSubFolders;
        ClearSearch();
        return;
    }

    StringUtil::Copy(_searchQuery, query, sizeof(_searchQuery));
    _searchIncludeSubFolders = includeSubFolders;

    if (includeSubFolders)
    {
        const auto direction = GetRomBrowserDisplaySettings().sortMode == RomBrowserSortMode::NameAscending
            ? SdFolderSortDirection::Ascending : SdFolderSortDirection::Descending;
        _searchSortDirection = direction;
        // The current results stay visible until the new search completes
        auto queryCopy = SharedPtr<std::array<char, sizeof(_searchQuery)>>::MakeShared();
        StringUtil::Copy(queryCopy->data(), _searchQuery, queryCopy->size());
        _searchTask = _ioTaskQueue->Enqueue([this, queryCopy, direction] (const vu8& cancelRequested)
        {
            RecursiveSdFolderFactory factory { &_fileTypeProvider };
            auto result = factory.CreateFromPathRecursive(".", queryCopy->data(), cancelRequested, direction);
            if (!result)
                return TaskResult<SharedPtr<SdFolder>>::Canceled();
            return TaskResult<SharedPtr<SdFolder>>::Completed(result);
        });
    }
    else
    {
        _romBrowserViewModel.Reset();
        _searchResultFolder.Reset();
        RecreateRomBrowserViewModel();
        _searchResultsUpdated = true;
    }
}

void RomBrowserController::ClearSearch()
{
    CancelSearchTask();
    if (!_searchQuery[0] && !_searchResultFolder)
        return;

    _searchQuery[0] = 0;
    _romBrowserViewModel.Reset();
    _searchResultFolder.Reset();
    RecreateRomBrowserViewModel();
    _searchResultsUpdated = true;
}

void RomBrowserController::CancelSearchTask()
{
    // A running task stops early when canceled, and its task queue destroys it,
    // together with its result, once it completes.
    _searchTask.CancelTask();
}

void RomBrowserController::RecreateRomBrowserViewModel(const char* initialSelectedFileName)
{
    const char* searchQuery = _searchQuery[0] ? _searchQuery : nullptr;
    _romBrowserViewModel = SharedPtr<RomBrowserViewModel>::MakeShared(
        this, initialSelectedFileName, searchQuery);
}

bool RomBrowserController::ConsumeSearchResultsUpdated()
{
    bool result = _searchResultsUpdated;
    _searchResultsUpdated = false;
    return result;
}

void RomBrowserController::SetRomBrowserDisplaySettings(
    const RomBrowserDisplaySettings& romBrowserDisplaySettings)
{
    _appSettingsService->GetAppSettings().romBrowserDisplaySettings = romBrowserDisplaySettings;
    _saveSettingsPending = true;
    _stateMachine.Fire(RomBrowserStateTrigger::ChangeDisplayMode);
}

void RomBrowserController::Update()
{
    _stateMachine.Update();
    if (_stateMachine.HasStateChanged())
    {
        HandleTrigger();
    }
    switch (_stateMachine.GetCurrentState())
    {
        case RomBrowserState::Start:
        {
            LOG_DEBUG("RomBrowserState::Start\n");
            const auto& lastUsed = _appSettingsService->GetAppSettings().lastUsedFilePath;
            if (strlen(lastUsed.GetString()) != 0)
            {
                NavigateToPath(lastUsed.GetString());
            }
            else
            {
                NavigateToPath("/");
            }
            break;
        }
        case RomBrowserState::LoadingFolder:
        {
            if (_navigateTask.GetTask().IsCompletedSuccessfully())
            {
                _navigateTask.Dispose();
                _stateMachine.Fire(RomBrowserStateTrigger::FolderLoadDone);
            }
            break;
        }
        case RomBrowserState::Launching:
        default:
        {
            break;
        }
    }

    // Subfolder search results also arrive after the search sheet was closed
    if (_searchTask.IsValid() && _searchTask.GetTask().IsCompleted())
    {
        if (_searchTask.GetTask().IsCompletedSuccessfully())
        {
            _romBrowserViewModel.Reset();
            _searchResultFolder = _searchTask.GetTask().GetResult();
            RecreateRomBrowserViewModel();
            _searchResultsUpdated = true;
        }
        _searchTask.Dispose();
    }
}

void RomBrowserController::HandleTrigger()
{
    switch (_stateMachine.GetLastTrigger())
    {
        case RomBrowserStateTrigger::Navigate:
            HandleNavigateTrigger();
            break;

        case RomBrowserStateTrigger::FolderLoadDone:
            HandleFolderLoadDoneTrigger();
            break;

        case RomBrowserStateTrigger::Launch:
            HandleLaunchTrigger();
            break;

        case RomBrowserStateTrigger::ChangeDisplayMode:
            HandleChangeDisplayModeTrigger();
            break;

        case RomBrowserStateTrigger::GotoSettingsScreen:
            HandleGotoSettingsScreenTrigger();
            break;

        default:
            break;
    }
}

void RomBrowserController::HandleNavigateTrigger()
{
    LOG_DEBUG("RomBrowserStateTrigger::Navigate\n");
    // Opening or leaving a folder ends the search. The subfolder results are
    // dropped once the folder is loaded, as the view model still uses them.
    CancelSearchTask();
    _searchQuery[0] = 0;
    _searchIncludeSubFolders = false;
    _navigateTask =_ioTaskQueue->Enqueue([this] (const vu8& cancelRequested)
    {
        if (!_coverRepository)
        {
            _coverRepository = std::make_unique<CoverRepository>();
            _coverRepository->Initialize();
        }
        if (!_iconRepository)
        {
            _iconRepository = std::make_unique<IconRepository>();
            _iconRepository->Initialize();
        }
        if (!_bannerRepository)
        {
            _bannerRepository = std::make_unique<BannerRepository>();
            _bannerRepository->Initialize();
        }
        if (!_cheatRepository)
        {
            _cheatRepository = UsrCheatRepositoryFactory().FromUsrCheatDat("/_pico/usrcheat.dat");
            if (!_cheatRepository)
            {
                // When usrcheat.dat is not found or cannot be read use a dummy empty cheat repository
                _cheatRepository = std::make_unique<EmptyCheatRepository>();
            }
        }

        u64 startTick = gTickCounter.GetValue();
        _navigateFileName = nullptr;
        if (strcmp(_navigatePath, "/") != 0) // can't f_stat on root dir
        {
            FILINFO fileInfo;
            if (f_stat(_navigatePath, &fileInfo) != FR_OK)
            {
                StringUtil::Copy(_navigatePath, "/", sizeof(_navigatePath) / sizeof(_navigatePath[0]));
            }
            else if (!(fileInfo.fattrib & AM_DIR))
            {
                _navigateFileName = strrchr(_navigatePath, '/') + 1;
                _navigateFileName[-1] = 0;
            }
        }
        f_chdir(_navigatePath);
        SdFolderFactory sdFolderFactory { &_fileTypeProvider };
        _newSdFolder = sdFolderFactory.CreateFromPath(".");
        _newFolderPath = ReadCurrentDirectoryPath();
        u64 endTick = gTickCounter.GetValue();
        LOG_DEBUG("Loading files in folder took: %d us\n", (u32)TickCounter::TicksToMicroSeconds(endTick - startTick));
        return TaskResult<void>::Completed();
    });
}

void RomBrowserController::HandleFolderLoadDoneTrigger()
{
    LOG_DEBUG("RomBrowserStateTrigger::FolderLoadDone\n");
    _romBrowserViewModel.Reset();
    _searchResultFolder.Reset();
    _sdFolder = SharedPtr<SdFolder>(_newSdFolder.release());
    _currentFolderPath = std::move(_newFolderPath);
    RecreateRomBrowserViewModel(_navigateFileName);
}

void RomBrowserController::HandleLaunchTrigger()
{
    CancelSearchTask();
    LOG_DEBUG("RomBrowserStateTrigger::Launch\n");
    _ioTaskQueue->Enqueue([this] (const vu8& cancelRequested)
    {
        UpdateLastUsedFilepath();
        SetPicoLoaderParams();
        LoadCheats();
        return TaskResult<void>::Completed();
    });
}

void RomBrowserController::HandleChangeDisplayModeTrigger()
{
    LOG_DEBUG("RomBrowserStateTrigger::ChangeDisplayMode\n");
    const auto direction = GetRomBrowserDisplaySettings().sortMode == RomBrowserSortMode::NameAscending
        ? SdFolderSortDirection::Ascending : SdFolderSortDirection::Descending;
    if (_searchIncludeSubFolders && _searchQuery[0] && direction != _searchSortDirection)
    {
        std::array<char, sizeof(_searchQuery)> query;
        StringUtil::Copy(query.data(), _searchQuery, query.size());
        ApplySearch(query.data(), true);
        return;
    }
    RecreateRomBrowserViewModel();
}

void RomBrowserController::HandleGotoSettingsScreenTrigger()
{
    CancelSearchTask();
    gProcessManager.Goto<SettingsProcess>();
}

void RomBrowserController::UpdateLastUsedFilepath()
{
    f_getcwd(_navigatePath, sizeof(_navigatePath) / sizeof(_navigatePath[0]));
    int idx = strlcat(_navigatePath, "/", sizeof(_navigatePath));
    if (_navigatePath[idx - 2] == '/')
    {
        _navigatePath[idx - 1] = 0;
    }
    if (_triggerFileInfo.GetParentPath())
    {
        // File found by a subfolder search
        strlcat(_navigatePath, _triggerFileInfo.GetParentPath(), sizeof(_navigatePath));
        strlcat(_navigatePath, "/", sizeof(_navigatePath));
    }
    strlcat(_navigatePath, _triggerFileInfo.GetFileName(), sizeof(_navigatePath));
    _appSettingsService->GetAppSettings().lastUsedFilePath = _navigatePath;
    _appSettingsService->Save();
}

void RomBrowserController::SetPicoLoaderParams() const
{
    auto loadParams = pload_getLoadParams();
    loadParams->savePath[0] = 0;
    loadParams->arguments[0] = 0;
    loadParams->argumentsLength = 0;
    if (_triggerFileInfo.GetFileType()->TrySetLaunchParameters(loadParams, _navigatePath))
    {
        gProcessManager.Goto<PicoLoaderProcess>();
    }
    else
    {
        LOG_FATAL("Failed to set launch parameters.\n");
    }
}

void RomBrowserController::LoadCheats() const
{
    auto cheats = _cheatRepository->GetCheatsForGame(_triggerFileInfo.GetFastFileRef());
    auto cheatData = PicoLoaderCheatDataFactory().CreateCheatData(cheats);
    pload_setCheatData(cheatData);
}
