#pragma once

#include <windows.h>

namespace rpusb
{
    constexpr UINT8 kVendorRequestInit = 0xA0;
    constexpr UINT8 kVendorRequestModeSet = 0xA1;
    constexpr UINT8 kVendorRequestPing = 0xA2;
    constexpr UINT8 kVendorRequestStats = 0xA3;

    enum class PixelFormat : UINT32
    {
        Rgb565 = 0,
        Bgra8888 = 1,
    };

    inline constexpr UINT32 DefaultBulkPacketBytes = 16 * 1024;
    inline constexpr UINT32 DefaultInterruptPacketBytes = 64;
    inline constexpr UINT32 DefaultMaxFrameBytes = 480 * 320 * 2; // RGB565
}
