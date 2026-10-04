#pragma once
enum class UiScreen { Map, List, Squawk7500, Detail, Menu, WifiList, WifiPassword, WifiConnecting };
constexpr UiScreen tabDestination(UiScreen screen)
{
    return screen == UiScreen::Map ? UiScreen::List :
           screen == UiScreen::List ? UiScreen::Squawk7500 :
           screen == UiScreen::Squawk7500 || screen == UiScreen::Detail ? UiScreen::Map : screen;
}
constexpr UiScreen escapeDestination(UiScreen screen, UiScreen menuReturn)
{
    return screen == UiScreen::Detail ? UiScreen::List :
           screen == UiScreen::List || screen == UiScreen::Squawk7500 ? UiScreen::Map :
           screen == UiScreen::Menu ? menuReturn :
           screen == UiScreen::WifiList ? UiScreen::Menu :
           screen == UiScreen::WifiPassword || screen == UiScreen::WifiConnecting ? UiScreen::WifiList : screen;
}
