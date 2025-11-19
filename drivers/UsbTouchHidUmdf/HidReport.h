#pragma once

#include <hidport.h>

// HID Input Report structure matching the report descriptor
#pragma pack(push, 1)
struct HID_TOUCH_INPUT_REPORT
{
    UINT8 ReportId;         // Report ID (1)
    UINT8 TipSwitch : 1;    // Tip Switch
    UINT8 InRange : 1;      // In Range
    UINT8 Padding : 6;      // Padding (6 bits)
    UINT8 ContactId;        // Contact ID
    UINT16 X;               // X coordinate
    UINT16 Y;               // Y coordinate
    UINT8 ContactCount;     // Contact Count
};
#pragma pack(pop)

inline constexpr UCHAR g_RpTouchReportDescriptor[] = {
    0x05, 0x0D,       // Usage Page (Digitizers)
    0x09, 0x04,       // Usage (Touch Screen)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID 1
    0x09, 0x22,       //   Usage (Finger)
    0xA1, 0x02,       //   Collection (Logical)
    0x09, 0x42,       //     Usage (Tip Switch)
    0x09, 0x32,       //     Usage (In Range)
    0x15, 0x00,       //     Logical Min (0)
    0x25, 0x01,       //     Logical Max (1)
    0x75, 0x01,       //     Report Size (1)
    0x95, 0x02,       //     Report Count (2)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0x95, 0x06,       //     Report Count (6) padding
    0x81, 0x03,       //     Input (Const,Var,Abs)
    0x09, 0x51,       //     Usage (Contact ID)
    0x75, 0x08,
    0x95, 0x01,
    0x81, 0x02,
    0x05, 0x01,       //     Usage Page (Generic Desktop)
    0x09, 0x30,       //     Usage (X)
    0x09, 0x31,       //     Usage (Y)
    0x16, 0x00, 0x00, //     Logical Min (0)
    0x26, 0x1F, 0x03, //     Logical Max (799)
    0x36, 0x00, 0x00, //     Physical Min (0)
    0x46, 0x1F, 0x03, //     Physical Max (799)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x02,       //     Report Count (2)
    0x81, 0x02,       //     Input (Data,Var,Abs)
    0xC0,             //   End Collection
    0x55, 0x0C,       // Unit Exponent -4
    0x09, 0x54,       // Usage (Contact Count)
    0x25, 0x02,       // Logical Max (2)
    0x75, 0x08,
    0x95, 0x01,
    0x81, 0x02,
    0x09, 0x55,       // Usage (Contact Count Maximum)
    0xB1, 0x02,       // Feature (Data,Var,Abs)
    0xC0              // End Collection
};
