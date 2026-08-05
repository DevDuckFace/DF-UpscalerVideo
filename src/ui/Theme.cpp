#include "ui/Theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QLatin1String>
#include <QSettings>
#include <QStyle>
#include <QStyleHints>

#include <spdlog/spdlog.h>

namespace {
constexpr char kThemeSettingsKey[] = "ui/theme";
}

namespace dfu {

QString themeKey(ThemePreference preference)
{
    switch (preference) {
    case ThemePreference::Light:
        return QStringLiteral("light");
    case ThemePreference::Dark:
        return QStringLiteral("dark");
    case ThemePreference::System:
        break;
    }
    return QStringLiteral("system");
}

ThemePreference themeFromKey(const QString& key)
{
    if (key.compare(QLatin1String("light"), Qt::CaseInsensitive) == 0) {
        return ThemePreference::Light;
    }
    if (key.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0) {
        return ThemePreference::Dark;
    }
    return ThemePreference::System;
}

ThemePreference loadThemePreference()
{
    QSettings settings;
    return themeFromKey(
        settings.value(QLatin1String(kThemeSettingsKey), QStringLiteral("system")).toString());
}

void saveThemePreference(ThemePreference preference)
{
    QSettings settings;
    settings.setValue(QLatin1String(kThemeSettingsKey), themeKey(preference));
}

void applyThemePreference(ThemePreference preference)
{
    QStyleHints* hints = QGuiApplication::styleHints();

    // Fusion, deliberately, on every platform and in both schemes.
    //
    // The native Windows 10 style (windowsvista) draws through uxtheme and has
    // no dark variant: it ignores the colour scheme request entirely, which
    // leaves a light UI with dark-mode window decorations. Fusion honours
    // Qt::ColorScheme properly and gives one predictable appearance across
    // Windows versions, which matters more here than matching the native
    // control look.
    if (QApplication::style() && QApplication::style()->name() != QLatin1String("fusion")) {
        QApplication::setStyle(QStringLiteral("Fusion"));
    }

    // Widgets that derive colours from the palette pick this up through
    // QEvent::PaletteChange; anything caching a colour must also watch
    // QStyleHints::colorSchemeChanged.
    switch (preference) {
    case ThemePreference::System:
        hints->unsetColorScheme();
        break;
    case ThemePreference::Light:
        hints->setColorScheme(Qt::ColorScheme::Light);
        break;
    case ThemePreference::Dark:
        hints->setColorScheme(Qt::ColorScheme::Dark);
        break;
    }

    spdlog::info("Theme preference: {} (effective scheme: {})", themeKey(preference).toStdString(),
                 hints->colorScheme() == Qt::ColorScheme::Dark ? "dark" : "light");
}

} // namespace dfu
