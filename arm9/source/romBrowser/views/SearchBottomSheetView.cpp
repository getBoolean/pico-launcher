#include "common.h"
#include <string.h>
#include "core/StringUtil.h"
#include "gui/GraphicsContext.h"
#include "gui/input/InputProvider.h"
#include "themes/material/MaterialColorScheme.h"
#include "themes/IFontRepository.h"
#include "../IRomBrowserController.h"
#include "SearchBottomSheetView.h"

#define TITLE_LABEL_X       20
#define TITLE_LABEL_Y       4
#define PATH_LABEL_Y        20
#define QUERY_LABEL_X       20
#define QUERY_LABEL_Y       36
#define KEYBOARD_X          18
#define KEYBOARD_Y          56
#define HINT_LABEL_X        12
#define HINT_LABEL_Y        140

SearchBottomSheetView::SearchBottomSheetView(IRomBrowserController* controller,
    const MaterialColorScheme* materialColorScheme,
    const IFontRepository* fontRepository)
    : _controller(controller)
    , _materialColorScheme(materialColorScheme)
    , _titleLabel(Label2DView::CreateShared(128, 16, 25, fontRepository->GetFont(FontType::Medium11)))
    , _pathLabel(Label2DView::CreateShared(216, 14,
        controller->GetCurrentFolderPath() ? strlen(controller->GetCurrentFolderPath()) : 32,
        fontRepository->GetFont(FontType::Regular10)))
    , _queryLabel(Label2DView::CreateShared(216, 14, MAX_TEXT_LENGTH + 1, fontRepository->GetFont(FontType::Regular10)))
    , _hintLabel(Label2DView::CreateShared(138, 14, 32, fontRepository->GetFont(FontType::Regular10)))
    , _clearLabel(Label2DView::CreateShared(44, 14, 10, fontRepository->GetFont(FontType::Regular10)))
    , _closeLabel(Label2DView::CreateShared(48, 14, 10, fontRepository->GetFont(FontType::Regular10)))
    , _keyboardView(KeyboardView::CreateShared(fontRepository->GetFont(FontType::Regular10)))
{
    _text[0] = 0;
    _titleLabel->SetText(u"Search");
    AddChildTail(_titleLabel.GetPointer());
    _pathLabel->SetText(controller->GetCurrentFolderPath()
        ? controller->GetCurrentFolderPath() : "Folder path unavailable");
    _pathLabel->SetEllipsisStyle(LabelView::EllipsisStyle::Marquee);
    AddChildTail(_pathLabel.GetPointer());
    AddChildTail(_queryLabel.GetPointer());
    AddChildTail(_hintLabel.GetPointer());
    _clearLabel->SetText(u"Y: Clear");
    _closeLabel->SetText(u"B: Close");
    AddChildTail(_clearLabel.GetPointer());
    AddChildTail(_closeLabel.GetPointer());
    AddChildTail(_keyboardView.GetPointer());

    _keyboardView->SetOnKeyPress(&SearchBottomSheetView::OnKeyPress, this);

    // Restore the active search, if any
    const char* prevQuery = controller->GetSearchQuery();
    if (prevQuery && prevQuery[0])
    {
        StringUtil::Copy(_text, prevQuery, MAX_TEXT_LENGTH);
    }
    _includeSubFolders = controller->IsSearchIncludingSubFolders();

    UpdateQueryLabel();
    UpdateScopeHint();
}

void SearchBottomSheetView::UpdateScopeHint()
{
    _hintLabel->SetText(_includeSubFolders
        ? u"X: Current folder only" : u"X: Include subfolders");
}

void SearchBottomSheetView::ClearQuery()
{
    _text[0] = 0;
    _includeSubFolders = false;
    UpdateQueryLabel();
    UpdateScopeHint();
    ApplySearch();
}

void SearchBottomSheetView::OnKeyPress(char key, void* context)
{
    auto self = static_cast<SearchBottomSheetView*>(context);
    self->HandleKeyPress(key);
}

void SearchBottomSheetView::HandleKeyPress(char key)
{
    if (key == '\b')
    {
        u32 len = strlen(_text);
        if (len > 0)
            _text[len - 1] = 0;
    }
    else
    {
        u32 len = strlen(_text);
        if (len < MAX_TEXT_LENGTH - 1)
        {
            _text[len] = key;
            _text[len + 1] = 0;
        }
    }

    UpdateQueryLabel();
    ApplySearch();
}

void SearchBottomSheetView::UpdateQueryLabel()
{
    if (_text[0])
    {
        char display[MAX_TEXT_LENGTH + 2];
        u32 len = strlen(_text);
        memcpy(display, _text, len);
        display[len] = '|';
        display[len + 1] = 0;
        _queryLabel->SetText(display);
    }
    else
    {
        _queryLabel->SetText("|");
    }
}

void SearchBottomSheetView::ApplySearch()
{
    _controller->ApplySearch(_text, _includeSubFolders);
}

void SearchBottomSheetView::Update()
{
    BottomSheetView::Update();

    _titleLabel->SetPosition(TITLE_LABEL_X, _position.y + TITLE_LABEL_Y);
    _pathLabel->SetPosition(QUERY_LABEL_X, _position.y + PATH_LABEL_Y);
    _queryLabel->SetPosition(QUERY_LABEL_X, _position.y + QUERY_LABEL_Y);
    _hintLabel->SetPosition(HINT_LABEL_X, _position.y + HINT_LABEL_Y);
    _clearLabel->SetPosition(154, _position.y + HINT_LABEL_Y);
    _closeLabel->SetPosition(202, _position.y + HINT_LABEL_Y);
    _keyboardView->SetPosition(KEYBOARD_X, _position.y + KEYBOARD_Y);
}

void SearchBottomSheetView::Draw(GraphicsContext& graphicsContext)
{
    graphicsContext.SetClipArea(GetBounds());
    u32 oldPrio = graphicsContext.SetPriority(1);
    {
        auto background = _materialColorScheme->GetColor(md::sys::color::surfaceContainerLow);
        _titleLabel->SetBackgroundColor(background);
        _titleLabel->SetForegroundColor(_materialColorScheme->onSurface);
        _pathLabel->SetBackgroundColor(background);
        _pathLabel->SetForegroundColor(_materialColorScheme->onSurfaceVariant);
        _queryLabel->SetBackgroundColor(background);
        _queryLabel->SetForegroundColor(_materialColorScheme->onSurface);
        _hintLabel->SetBackgroundColor(background);
        _hintLabel->SetForegroundColor(_materialColorScheme->onSurfaceVariant);
        _clearLabel->SetBackgroundColor(background);
        _clearLabel->SetForegroundColor(_materialColorScheme->onSurfaceVariant);
        _closeLabel->SetBackgroundColor(background);
        _closeLabel->SetForegroundColor(_materialColorScheme->onSurfaceVariant);
        _keyboardView->SetBackgroundColor(background);
        _keyboardView->SetForegroundColor(_materialColorScheme->onSurface);
        _keyboardView->SetHighlightColor(
            _materialColorScheme->GetColor(md::sys::color::secondaryContainer));
        BottomSheetView::Draw(graphicsContext);
    }
    graphicsContext.SetPriority(oldPrio);
    graphicsContext.ResetClipArea();
}

bool SearchBottomSheetView::HandleInput(
    const InputProvider& inputProvider, FocusManager& focusManager)
{
    if (inputProvider.Triggered(InputKey::B))
    {
        Close();
        return true;
    }
    if (inputProvider.Triggered(InputKey::Y))
    {
        ClearQuery();
        return true;
    }
    if (inputProvider.Triggered(InputKey::X))
    {
        _includeSubFolders = !_includeSubFolders;
        UpdateScopeHint();
        ApplySearch();
        return true;
    }
    return false;
}

SharedPtr<View> SearchBottomSheetView::MoveFocus(const SharedPtr<View>& currentFocus,
    FocusMoveDirection direction, View* source)
{
    if (currentFocus.GetPointer() == _keyboardView.GetPointer())
    {
        return _keyboardView->MoveFocus(currentFocus, direction, this);
    }

    return nullptr;
}

void SearchBottomSheetView::Close()
{
    _controller->HideSearch();
}

