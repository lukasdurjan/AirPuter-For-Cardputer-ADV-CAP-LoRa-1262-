#include "ui_state.h"
// Compile-time checks of the requested navigation behavior.
static_assert(tabDestination(UiScreen::Map) == UiScreen::List, "Tab opens the flight list");
static_assert(tabDestination(UiScreen::List) == UiScreen::Squawk7500, "Second Tab opens the 7500 list");
static_assert(tabDestination(UiScreen::Squawk7500) == UiScreen::Map, "Third Tab returns to the map");
static_assert(tabDestination(UiScreen::Detail) == UiScreen::Map, "Tab from details returns to the map");
static_assert(tabDestination(UiScreen::WifiPassword) == UiScreen::WifiPassword, "Tab cannot leave a password form");
static_assert(escapeDestination(UiScreen::Detail, UiScreen::Map) == UiScreen::List, "Esc closes flight details to the list");
static_assert(escapeDestination(UiScreen::Menu, UiScreen::Detail) == UiScreen::Detail, "Menu returns to the originating detail");
static_assert(escapeDestination(UiScreen::WifiPassword, UiScreen::List) == UiScreen::WifiList, "Cancel password entry returns to networks");
static_assert(escapeDestination(UiScreen::WifiList, UiScreen::Map) == UiScreen::Menu, "Esc from WiFi returns to settings");
