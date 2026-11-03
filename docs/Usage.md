# Using Pico Launcher
This document will outline the different settings and functionalities of Pico Launcher.

## Pico Launcher interface
When Pico Launcher is started, this is how your screen will look like.

![Example screen](./images/Horizontal.png)

From here you can browse your SD card to launch homebrew and games.

- DPAD: Move the selector.
- A: Open a folder, or to launch a homebrew or game.
- B: Go to the parent folder or close a menu.
- L and R: Scroll quickly when there are many items in a folder.
- Y: Open the cheats panel (see [Cheats](Cheats.md)).

The back arrow on the top left of the bottom screen can also be used to go up to the parent folder.

Touch input is also supported.

## Searching
To search for a file, move the selector to the search icon next to the cogwheel and press A. A keyboard opens on the bottom screen.

- DPAD and A: Type on the keyboard. You can also tap the keys.
- X: Switch between the current folder and including subfolders, up to 8 levels deep. At most 256 of the best subfolder results are shown. The bottom hint tells you what pressing X will do next.
- Y: Clear the search and return to current-folder-only scope.
- B: Close the keyboard. The results stay in the file browser, so you can launch them.

Search ignores upper and lower case and matches any part of the filename. It also handles small typing mistakes automatically: for example, `gmae` can find `game.nds`. Literal matches appear before approximate matches, including when names are sorted from Z to A. Very short queries use literal matching only.

The full current folder path appears above the keyboard in either scope. Long paths scroll through their complete text. Closing and reopening search restores the query and scope; entering or leaving a folder ends the search. While a search is active, the search icon is highlighted.

With an empty query, X changes the scope for the next search without scanning subfolders. Deleting the last character keeps that scope choice; Y resets it.

Subfolder results whose full launch path exceeds 255 bytes are skipped.

## Settings menu
The settings menu can be accessed by using the DPAD to move the selector to the cogwheel icon and pressing A. When in the settings menu, press the B button will to return to the file browser.

![Settings menu](./images/SettingsPage.png)

Currently, the only settings available are the display mode, and the sorting mode (More settings are available [in the settings file](#settings)). Here is how each layout looks like.

<table>
    <tr>
        <th>Horizontal Grid</th>
        <th>Vertical Grid</th>
        <th>Banner List</th>
        <th>Coverflow</th>
    </tr>
    <tr>
        <td><img src="./images/Horizontal.png"/></td>
        <td><img src="./images/Vertical.png"/></td>
        <td><img src="./images/List.png"/></td>
        <td><img src="./images/Coverflow.png"/></td>
    </tr>
</table>

## Settings
Settings are stored on your SD card in `/_pico/settings.json`. They can be edited with any text editor. The following settings are available:
- `language` - Display language for Pico Launcher. Currently, only `english` is supported. Other languages may be supported later.
- `romBrowserLayout` - Specified how folder contents are displayed. This setting can be changed in Pico Launcher directly.
- `romBrowserSortMode` - Specified if folder contents should be sorted from A to Z (`NameAscending`), or from Z to A (`NameDescending`). This setting can be changed from within Pico Launcher.
- `theme`: Specifies the folder name of the theme to use. If the theme cannot be found, a default fallback theme will be used.
- `lastUsedFilePath` - Specifies the path of the most recently launched homebrew or game, such that it can be selected the next time Pico Launcher is started. It is automatically updated by Pico Launcher.
- `fileAssociations` - See [FileAssociations.md](/docs/FileAssociations.md) for information about how to use this setting.
