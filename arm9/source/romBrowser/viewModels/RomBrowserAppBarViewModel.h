#pragma once
#include "../IRomBrowserController.h"

/// @brief View model for the rom browser app bar
class RomBrowserAppBarViewModel
{
public:
    explicit RomBrowserAppBarViewModel(IRomBrowserController* romBrowserController)
        : _romBrowserController(romBrowserController) { }

    void NavigateUp()
    {
        _romBrowserController->NavigateUp();
    }

    void ShowDisplaySettings()
    {
        _romBrowserController->ShowDisplaySettings();
    }

    void ShowSearch()
    {
        _romBrowserController->ShowSearch();
    }

    bool IsSearchActive() const
    {
        return _romBrowserController->IsSearchActive();
    }

    constexpr RomBrowserLayout GetRomBrowserLayout() const
    {
        return _romBrowserController->GetRomBrowserDisplaySettings().layout;
    }

private:
    IRomBrowserController* _romBrowserController;
};
