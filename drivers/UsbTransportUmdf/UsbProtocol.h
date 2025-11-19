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

    // Frame chunking constants
    inline constexpr UINT32 ChunkSize = 16 * 1024;  // 16KB chunks for USB bulk transfer
    inline constexpr UINT32 MaxFrameSize = 800 * 480 * 2;  // 800x480 RGB565 = 768,000 bytes
    inline constexpr UINT32 MaxChunksPerFrame = (MaxFrameSize + ChunkSize - 1) / ChunkSize;  // 47 chunks

    // Touch input protocol structures
    enum class InterruptPacketType : UINT8
    {
        Status = 0,
        Touch = 1,
    };

    // Touch contact data
    #pragma pack(push, 1)
    struct TouchContact
    {
        UINT8 ContactId;        // Contact identifier (0-9)
        UINT8 TipSwitch : 1;    // 1 if finger is touching
        UINT8 InRange : 1;      // 1 if finger is in range
        UINT8 Reserved : 6;     // Reserved bits
        UINT16 X;               // X coordinate (0-799 for 800x480)
        UINT16 Y;               // Y coordinate (0-479 for 800x480)
    };

    // Status packet data
    struct StatusPacket
    {
        UINT32 LastFrameAcked;  // Last frame acknowledged by device
        UINT8 ErrorCode;        // Error code (0 = no error)
        UINT8 Reserved[3];      // Reserved for future use
    };

    // Interrupt packet from device
    struct InterruptPacket
    {
        InterruptPacketType PacketType;
        union
        {
            TouchContact Touch;
            StatusPacket Status;
        } Data;
    };
    #pragma pack(pop)

    // Maximum number of simultaneous touch contacts
    inline constexpr UINT32 MaxTouchContacts = 2;
}
