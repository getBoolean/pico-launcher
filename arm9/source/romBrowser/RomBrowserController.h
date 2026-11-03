#pragma once
#include <memory>
#include "core/SharedPtr.h"
#include "SdFolder.h"
#include "viewModels/RomBrowserViewModel.h"
#include "RomBrowserStateMachine.h"
#include "core/task/TaskQueue.h"
#include "IRomBrowserController.h"
#include "CoverRepository.h"
#include "IconRepository.h"
#include "BannerRepository.h"
#include "FileType/ExtensionFileTypeProvider.h"
#include "services/settings/IAppSettingsService.h"
#include "cheats/ICheatRepository.h"

class RomBrowserController : public IRomBrowserController
{
public:
    RomBrowserController(IAppSettingsService* appSettingsService,
        TaskQueueBase* ioTaskQueue, TaskQueueBase* bgTaskQueue);
    ~RomBrowserController();

    void NavigateUp() override
    {
        NavigateToPath("..");
    }

    void NavigateToPath(const TCHAR* name) override;
    void LaunchFile(const FileInfo& fileInfo) override;
    void ShowGameInfo(const FileInfo& fileInfo) override;
    void HideGameInfo() override;
    void ShowDisplaySettings() override;
    void HideDisplaySettings() override;
    void GotoSettingsScreen() override;

    void ShowSearch() override;
    void HideSearch() override;
    void ApplySearch(const char* query, bool includeSubFolders) override;
    bool ConsumeSearchResultsUpdated();
    const char* GetSearchQuery() const override { return _searchQuery; }
    const char* GetCurrentFolderPath() const override { return _currentFolderPath.get(); }
    void ClearSearch() override;
    void CancelSearchTask();
    bool IsSearchActive() const override { return _searchQuery[0] != 0; }
    bool IsSearchIncludingSubFolders() const override { return _searchIncludeSubFolders; }

    void Update() override;

    SharedPtr<const SdFolder> GetSdFolder() const override
    {
        return _searchResultFolder ? _searchResultFolder : _sdFolder;
    }

    const RomBrowserStateMachine& GetStateMachine() const override { return _stateMachine; }

    const SharedPtr<RomBrowserViewModel>& GetRomBrowserViewModel() override { return _romBrowserViewModel; }

    TaskQueueBase* GetIoTaskQueue() const override { return _ioTaskQueue; }
    TaskQueueBase* GetBgTaskQueue() const override { return _bgTaskQueue; }
    const ICoverRepository& GetCoverRepository() const override { return *_coverRepository; }
    const IIconRepository& GetIconRepository() const override { return *_iconRepository; }
    const IBannerRepository& GetBannerRepository() const override { return *_bannerRepository; }
    const ICheatRepository& GetCheatRepository() const override { return *_cheatRepository; }

    void SetRomBrowserDisplaySettings(const RomBrowserDisplaySettings& romBrowserDisplaySettings) override;

    const RomBrowserDisplaySettings& GetRomBrowserDisplaySettings() const override
    {
        return _appSettingsService->GetAppSettings().romBrowserDisplaySettings;
    }

    virtual const FileInfo& GetTriggerFileInfo() const override { return _triggerFileInfo; }

private:
    IAppSettingsService* _appSettingsService;
    TaskQueueBase* _ioTaskQueue;
    TaskQueueBase* _bgTaskQueue;

    SharedPtr<SdFolder> _sdFolder;
    SharedPtr<RomBrowserViewModel> _romBrowserViewModel;
    std::unique_ptr<SdFolder> _newSdFolder;
    std::unique_ptr<char[]> _newFolderPath;
    std::unique_ptr<char[]> _currentFolderPath;
    RomBrowserStateMachine _stateMachine;
    TCHAR _navigatePath[256];
    TCHAR* _navigateFileName;
    FileInfo _triggerFileInfo;
    QueueTask<void> _navigateTask;
    bool _saveSettingsPending = false;
    char _searchQuery[32] {};
    bool _searchIncludeSubFolders = false;
    SdFolderSortDirection _searchSortDirection = SdFolderSortDirection::Ascending;
    bool _searchResultsUpdated = false;
    /// @brief Subfolder search results; shown instead of _sdFolder when set.
    SharedPtr<SdFolder> _searchResultFolder;
    QueueTask<SharedPtr<SdFolder>> _searchTask;
    std::unique_ptr<CoverRepository> _coverRepository;
    std::unique_ptr<IconRepository> _iconRepository;
    std::unique_ptr<BannerRepository> _bannerRepository;
    ExtensionFileTypeProvider _fileTypeProvider;
    std::unique_ptr<ICheatRepository> _cheatRepository;

    void HandleTrigger();
    void HandleNavigateTrigger();
    void HandleFolderLoadDoneTrigger();
    void HandleLaunchTrigger();
    void HandleChangeDisplayModeTrigger();
    void HandleGotoSettingsScreenTrigger();
    void UpdateLastUsedFilepath();
    void SetPicoLoaderParams() const;
    void LoadCheats() const;
    void RecreateRomBrowserViewModel(const char* initialSelectedFileName = nullptr);
};
