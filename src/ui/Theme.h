#pragma once

#include <QString>

namespace dfu {

enum class ThemePreference
{
    System,
    Light,
    Dark
};

// Stable string form used for persistence. Not user-facing text.
QString themeKey(ThemePreference preference);
ThemePreference themeFromKey(const QString& key);

ThemePreference loadThemePreference();
void saveThemePreference(ThemePreference preference);

// Requires a QGuiApplication. Never call this from the headless entry point.
void applyThemePreference(ThemePreference preference);

} // namespace dfu
