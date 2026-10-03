#pragma once
enum class UiScreen { Map, List, Detail, Menu, WifiList, WifiPassword, WifiConnecting };
constexpr UiScreen tabDestination(UiScreen screen)
{
    return screen == UiScreen::Map ? UiScreen::List :
           screen == UiScreen::List || screen == UiScreen::Detail ? UiScreen::Map : screen;
}
constexpr UiScreen escapeDestination(UiScreen screen, UiScreen menuReturn)
{
    return screen == UiScreen::Detail ? UiScreen::List :
           screen == UiScreen::List ? UiScreen::Map :
           screen == UiScreen::Menu ? menuReturn :
           screen == UiScreen::WifiList ? UiScreen::Menu :
           screen == UiScreen::WifiPassword || screen == UiScreen::WifiConnecting ? UiScreen::WifiList : screen;
}
