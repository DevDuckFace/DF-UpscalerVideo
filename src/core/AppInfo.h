#pragma once

#include <QString>

// Single source of truth for product identity. The only other place the
// product name appears is the project() call in CMakeLists.txt.
namespace dfu {

inline constexpr char kAppName[] = "DF-UpscalerVideo";
inline constexpr char kOrgName[] = "DF-UpscalerVideo";
inline constexpr char kOrgDomain[] = "df-upscalervideo.local";

// spdlog logger name; also the log file stem.
inline constexpr char kLoggerName[] = "dfupscaler";

#ifndef DFU_VERSION_STRING
#    define DFU_VERSION_STRING "0.0.0-dev"
#endif
inline constexpr char kAppVersion[] = DFU_VERSION_STRING;

inline QString appName()
{
    return QString::fromLatin1(kAppName);
}

inline QString appVersion()
{
    return QString::fromLatin1(kAppVersion);
}

} // namespace dfu
