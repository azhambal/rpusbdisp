# Windows 11 Driver Architecture Plan
## RoboPeak USB Display with Touch Support

**Document Version:** 1.0
**Date:** 2025-11-19
**Target Platform:** Windows 11 (Build 22000+)
**Driver Framework:** UMDF v2.33

---

## Table of Contents
1. [Executive Summary](#executive-summary)
2. [System Architecture Overview](#system-architecture-overview)
3. [Component Details](#component-details)
4. [Data Flow](#data-flow)
5. [Protocol Specification](#protocol-specification)
6. [Driver Stack](#driver-stack)
7. [Installation and Deployment](#installation-and-deployment)
8. [Development Roadmap](#development-roadmap)
9. [Testing and Validation](#testing-and-validation)
10. [Performance Considerations](#performance-considerations)

---

## Executive Summary

This document describes the complete architecture for Windows 11 UMDF drivers supporting the RoboPeak USB display device (VID: 0x1FC9, PID: 0x0094). The solution consists of three cooperating user-mode drivers:

1. **UsbTransportUmdf** - USB communication layer
2. **UsbDisplayIdd** - Indirect Display Driver (IddCx)
3. **UsbTouchHidUmdf** - HID mini-driver for multitouch

### Key Features
- **Display:** 800x480 @ 60Hz, RGB565 pixel format
- **Touch:** 2-point multitouch, absolute positioning
- **USB:** High-Speed USB 2.0 (Bulk + Interrupt endpoints)
- **Security:** UMDF v2 user-mode isolation
- **Performance:** Hardware-accelerated pixel conversion, asynchronous frame processing

---

## System Architecture Overview

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                          Windows Applications                        │
│                    (GDI, DirectX, Windows Touch)                     │
└─────────────────────────────────────────────────────────────────────┘
                                    │
        ┌───────────────────────────┼───────────────────────────┐
        │                           │                           │
        ▼                           ▼                           ▼
┌────────────────┐        ┌────────────────┐        ┌────────────────┐
│   DWM/DXGI     │        │  Windows GDI   │        │  Touch Stack   │
│  Compositor    │        │                │        │   (PTP/HID)    │
└────────────────┘        └────────────────┘        └────────────────┘
        │                           │                           │
        └───────────────────────────┼───────────────────────────┘
                                    │
        ┌───────────────────────────┼───────────────────────────┐
        │                           │                           │
        ▼                           ▼                           ▼
┌────────────────┐        ┌────────────────┐        ┌────────────────┐
│   IddCx DDI    │        │ Desktop Window │        │  HID Class     │
│   Framework    │        │    Manager     │        │    Driver      │
└────────────────┘        └────────────────┘        └────────────────┘
        │                                                       │
        ▼                                                       ▼
┌───────────────────────────────────┐        ┌─────────────────────────┐
│     UsbDisplayIdd Driver          │        │  UsbTouchHidUmdf Driver │
│  (UMDF Indirect Display)          │        │  (UMDF HID Mini-Driver) │
│                                   │        │                         │
│  • IddCx adapter/monitor          │        │  • HID report descriptor│
│  • EDID generation                │        │  • Touch input reports  │
│  • Swap-chain processing          │        │  • Contact tracking     │
│  • Pixel conversion (BGRA→RGB565) │        │                         │
└───────────────────────────────────┘        └─────────────────────────┘
        │                                                       │
        │                                                       │
        └───────────────────────┬───────────────────────────────┘
                                │
                                ▼
                ┌───────────────────────────────┐
                │   UsbTransportUmdf Driver     │
                │   (UMDF USB Client)           │
                │                               │
                │  • USB pipe management        │
                │  • IOCTL interface            │
                │  • Frame buffering            │
                │  • Touch data routing         │
                │  • Vendor protocol            │
                └───────────────────────────────┘
                                │
                                ▼
                ┌───────────────────────────────┐
                │       WinUSB.sys              │
                │   (Microsoft USB Stack)       │
                └───────────────────────────────┘
                                │
                                ▼
                        ╔═══════════════╗
                        ║  USB Bus      ║
                        ╚═══════════════╝
                                │
                                ▼
                    ┌───────────────────────┐
                    │   RoboPeak USB Device │
                    │   VID:0x1FC9          │
                    │   PID:0x0094          │
                    │                       │
                    │  • 800x480 LCD        │
                    │  • Touch digitizer    │
                    │  • USB 2.0 HS         │
                    └───────────────────────┘
```

---

## Component Details

### 1. UsbTransportUmdf Driver

**Purpose:** Low-level USB communication layer providing IOCTL-based interface to upper drivers.

**Architecture:**
```
UsbTransportUmdf
│
├── Driver.cpp              (DriverEntry, WDF initialization)
├── Device.cpp              (Device context, USB pipe setup)
│   ├── USB Configuration
│   │   ├── Bulk Out Endpoint (Frame data)
│   │   ├── Bulk In Endpoint (Reserved)
│   │   └── Interrupt In Endpoint (Touch + Status)
│   │
│   └── Continuous Reader (Interrupt endpoint)
│
└── Queue.cpp               (IOCTL handlers)
    ├── IOCTL_RPUSB_PING
    ├── IOCTL_RPUSB_GET_VERSION
    ├── IOCTL_RPUSB_PUSH_FRAME
    ├── IOCTL_RPUSB_SET_MODE
    ├── IOCTL_RPUSB_GET_STATISTICS
    ├── IOCTL_RPUSB_REGISTER_LISTENER
    └── IOCTL_RPUSB_GET_TOUCH_DATA
```

**Key Structures:**

```cpp
// Device context
struct DEVICE_CONTEXT {
    WDFUSBDEVICE UsbDevice;
    WDFUSBINTERFACE UsbInterface;
    WDFUSBPIPE BulkOut;         // Frame transmission
    WDFUSBPIPE BulkIn;          // Reserved
    WDFUSBPIPE InterruptIn;     // Touch + status data
    RPUSB_STATISTICS Statistics;
    WDFQUEUE TouchDataQueue;    // Pending touch read requests
    WDFSPINLOCK TouchDataLock;
    RPUSB_TOUCH_DATA LatestTouchData;
};
```

**Vendor Protocol Commands:**
- `0xA0` - Initialize device
- `0xA1` - Set display mode
- `0xA2` - Ping (heartbeat)
- `0xA3` - Get statistics

**Frame Format:**
```cpp
struct RPUSB_FRAME_HEADER {
    UINT32 Width;           // Frame width (800)
    UINT32 Height;          // Frame height (480)
    UINT32 PixelFormat;     // RGB565 = 0
    UINT32 PayloadBytes;    // Width * Height * 2
};
// Followed by pixel data
```

---

### 2. UsbDisplayIdd Driver

**Purpose:** Expose USB display as Windows monitor using Indirect Display Driver model.

**Architecture:**
```
UsbDisplayIdd
│
├── Driver.cpp              (IddCx adapter initialization)
├── DisplayDevice.cpp       (Monitor creation, mode management)
│   ├── EDID Generation
│   │   └── 800x480@60Hz monitor descriptor
│   │
│   ├── Monitor Modes
│   │   └── 800x480, 16bpp, 60Hz
│   │
│   └── Swap-Chain Management
│       ├── EvtAssignSwapChain
│       ├── EvtUnassignSwapChain
│       └── EvtCommitModes
│
└── Pipeline.cpp            (Frame processing)
    ├── Present Loop Thread
    ├── Surface Acquisition (IddCxSwapChainGetBuffer)
    ├── Pixel Conversion (BGRA8888 → RGB565)
    ├── Frame Chunking
    └── USB Transport IOCTL
```

**Display Pipeline Flow:**
```
┌─────────────────────────────────────────────────────────┐
│  Windows Desktop (DXGI Swap Chain)                      │
└─────────────────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│  IddCxSwapChainSetDevice()                              │
│  • Assign swap chain to software processing             │
└─────────────────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│  Present Loop (Dedicated Thread)                        │
│  • Loop: IddCxSwapChainReleaseAndAcquireBuffer()        │
│  • Frequency: 60Hz (16.67ms interval)                   │
└─────────────────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│  IddCxSwapChainGetBuffer()                              │
│  • Acquire IDXGISurface from swap chain                 │
│  • Surface format: DXGI_FORMAT_B8G8R8A8_UNORM           │
└─────────────────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│  Surface Mapping (IDXGISurface::Map)                    │
│  • Map GPU/shared memory to CPU-accessible pointer      │
│  • Lock for reading                                     │
└─────────────────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│  Pixel Conversion (BGRA8888 → RGB565)                   │
│  • For each pixel:                                      │
│    R5 = (R8 & 0xF8) << 8                                │
│    G6 = (G8 & 0xFC) << 3                                │
│    B5 = (B8 & 0xF8) >> 3                                │
│    RGB565 = R5 | G6 | B5                                │
│  • Output: 800*480*2 = 768,000 bytes                    │
└─────────────────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│  Frame Chunking (Optional for large frames)             │
│  • Split into 16KB chunks                               │
│  • Send asynchronously                                  │
└─────────────────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│  IOCTL_RPUSB_PUSH_FRAME                                 │
│  • Send to UsbTransportUmdf                             │
│  • Includes frame header + pixel data                   │
└─────────────────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│  USB Bulk Transfer (WinUSB)                             │
│  • Endpoint: Bulk Out                                   │
│  • Transfer to device                                   │
└─────────────────────────────────────────────────────────┘
```

**EDID Structure:**
```cpp
// Simplified EDID for 800x480 display
const BYTE EDID_800x480[] = {
    // Header
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    // Manufacturer ID: RBP (RoboPeak)
    0x49, 0xE4,
    // Product code
    0x00, 0x00,
    // Serial number
    0x00, 0x00, 0x00, 0x00,
    // Week/Year
    0x01, 0x1E,
    // EDID version 1.4
    0x01, 0x04,
    // ... (full EDID in Edid.h)
};
```

---

### 3. UsbTouchHidUmdf Driver

**Purpose:** Expose touch digitizer as standard Windows HID device.

**Architecture:**
```
UsbTouchHidUmdf
│
├── Driver.cpp              (HID mini-driver initialization)
├── Device.cpp              (HID descriptor handling)
│   ├── IOCTL_HID_GET_DEVICE_DESCRIPTOR
│   ├── IOCTL_HID_GET_REPORT_DESCRIPTOR
│   ├── IOCTL_HID_GET_DEVICE_ATTRIBUTES
│   ├── IOCTL_HID_READ_REPORT
│   └── IOCTL_HID_GET_FEATURE
│
└── HidReport.h             (Report descriptor)
    ├── Touch Screen Descriptor (0x0D/0x04)
    ├── Max 2 contacts
    └── Absolute X/Y (0-799, 0-479)
```

**HID Report Descriptor:**
```
Usage Page: Digitizers (0x0D)
Usage: Touch Screen (0x04)
Collection: Application
  Report ID: 1
  Usage: Finger
  Collection: Logical
    Usage: Tip Switch         (1 bit)
    Usage: In Range           (1 bit)
    Padding                   (6 bits)
    Usage: Contact ID         (8 bits)
    Usage: X                  (16 bits, 0-799)
    Usage: Y                  (16 bits, 0-479)
  End Collection
  Usage: Contact Count        (8 bits, 0-2)
  Usage: Contact Max          (8 bits, feature)
End Collection
```

**HID Input Report Format:**
```cpp
struct HID_TOUCH_INPUT_REPORT {
    UINT8 ReportId;         // 0x01
    UINT8 TipSwitch : 1;    // Finger touching?
    UINT8 InRange : 1;      // Finger in range?
    UINT8 Padding : 6;      // Reserved
    UINT8 ContactId;        // 0-1 (2 contacts max)
    UINT16 X;               // 0-799
    UINT16 Y;               // 0-479
    UINT8 ContactCount;     // 1-2 active contacts
};
// Total: 8 bytes
```

---

## Data Flow

### Display Data Flow (Application → Device)

```
[Application]
    → GDI/DirectX draw commands
        → [DWM Compositor]
            → Composited desktop surface
                → [IddCx Framework]
                    → Swap chain assignment
                        → [UsbDisplayIdd::EvtAssignSwapChain]
                            → Start present loop
                                → [Present Loop Thread]
                                    ┌─────────────────────┐
                                    │ Every 16.67ms (60Hz)│
                                    └─────────────────────┘
                                            │
                                            ▼
                                    IddCxSwapChainReleaseAndAcquireBuffer()
                                            │
                                            ▼
                                    IddCxSwapChainGetBuffer() → IDXGISurface
                                            │
                                            ▼
                                    surface->Map() → BGRA8888 pixels
                                            │
                                            ▼
                                    [Pipeline::ConvertBGRAtoRGB565]
                                            │
                                            ▼
                                    [Pipeline::ChunkFrame] (optional)
                                            │
                                            ▼
                                    DeviceIoControl(IOCTL_RPUSB_PUSH_FRAME)
                                            │
                                            ▼
                                    [UsbTransportUmdf::Queue.cpp]
                                            │
                                            ▼
                                    WdfUsbTargetPipeFormatRequestForWrite()
                                            │
                                            ▼
                                    [WinUSB] Bulk OUT transfer
                                            │
                                            ▼
                                    [RoboPeak Device] LCD update
```

### Touch Data Flow (Device → Application)

```
[RoboPeak Touch Digitizer]
    → Touch event (X, Y, TipSwitch)
        → [USB Interrupt IN Endpoint]
            → Interrupt transfer (32-64 bytes)
                → [WinUSB] USB stack
                    → [UsbTransportUmdf::UsbInterruptCompletion]
                        → Parse InterruptPacket
                            │
                            ├─ PacketType = Status
                            │   → Update statistics
                            │   → Store LastFrameAcked
                            │
                            └─ PacketType = Touch
                                → Extract TouchContact data
                                    → [Store in DEVICE_CONTEXT]
                                        │
                                        ├─→ Complete pending IOCTL_RPUSB_GET_TOUCH_DATA
                                        │   (if UsbDisplayIdd is polling)
                                        │
                                        └─→ [UsbTouchHidUmdf]
                                            → Check pending IOCTL_HID_READ_REPORT
                                                → Format HID_TOUCH_INPUT_REPORT
                                                    → Complete request
                                                        → [HID Class Driver]
                                                            → [Windows Touch Stack]
                                                                → WM_TOUCH / WM_POINTER messages
                                                                    → [Application]
```

### Inter-Driver Communication

```
┌──────────────────────┐         ┌──────────────────────┐
│  UsbDisplayIdd       │         │  UsbTouchHidUmdf     │
└──────────────────────┘         └──────────────────────┘
           │                                 │
           │ DeviceIoControl()               │ DeviceIoControl()
           │ IOCTL_RPUSB_PUSH_FRAME          │ IOCTL_RPUSB_GET_TOUCH_DATA
           │                                 │
           └────────────┬────────────────────┘
                        │
                        ▼
           ┌──────────────────────────────┐
           │    UsbTransportUmdf          │
           │                              │
           │  Device Interface:           │
           │  GUID_DEVINTERFACE_          │
           │  RPUSB_TRANSPORT             │
           │                              │
           │  {A1D66B33-4584-4D4E-        │
           │   868C-9B7E7D671D4D}         │
           └──────────────────────────────┘
                        │
                        ▼
                   [WinUSB]
```

**Communication Method:**
- Upper drivers (IDD, Touch HID) open handle to UsbTransportUmdf via device interface GUID
- Use `CreateFile()` with `GUID_DEVINTERFACE_RPUSB_TRANSPORT`
- Send IOCTLs via `DeviceIoControl()`
- UsbTransportUmdf manages USB pipe coordination

---

## Protocol Specification

### USB Endpoints

| Endpoint | Type      | Direction | Max Packet | Usage                    |
|----------|-----------|-----------|------------|--------------------------|
| EP1 OUT  | Bulk      | Host→Dev  | 512 bytes  | Frame data transmission  |
| EP2 IN   | Bulk      | Dev→Host  | 512 bytes  | Reserved (future use)    |
| EP3 IN   | Interrupt | Dev→Host  | 64 bytes   | Touch events + status    |

### Vendor Control Requests

**All vendor requests use:**
- bmRequestType: 0x40 (Vendor, Device, Host→Device)
- wIndex: 0
- Data stage: varies by request

| bRequest | Name          | wValue  | wLength | Data           | Description                |
|----------|---------------|---------|---------|----------------|----------------------------|
| 0xA0     | Init          | 0       | 0       | -              | Initialize device          |
| 0xA1     | ModeSet       | mode    | 0       | -              | Set display mode (0=RGB565)|
| 0xA2     | Ping          | 0       | 0       | -              | Heartbeat / keep-alive     |
| 0xA3     | GetStats      | 0       | 8       | RPUSB_STATS    | Get device statistics      |

### Frame Protocol (Bulk OUT)

**Single Frame Transfer:**
```
┌──────────────────────────────────────────────────────────┐
│ RPUSB_FRAME_HEADER (16 bytes)                            │
├──────────────────────────────────────────────────────────┤
│ Width (4 bytes)           : 800 (0x00000320)             │
│ Height (4 bytes)          : 480 (0x000001E0)             │
│ PixelFormat (4 bytes)     : 0 (RGB565)                   │
│ PayloadBytes (4 bytes)    : 768000 (0x000BB800)          │
├──────────────────────────────────────────────────────────┤
│ Pixel Data (768,000 bytes)                               │
│ • RGB565 format (R5G6B5)                                 │
│ • Row-major order (top→bottom, left→right)               │
│ • Total: 800 * 480 * 2 bytes                             │
└──────────────────────────────────────────────────────────┘
Total: 768,016 bytes
```

**Chunked Frame Transfer (for optimization):**
```
Chunk 0:  Header(16) + Pixels[0..16367]     = 16,384 bytes
Chunk 1:  Pixels[16368..32751]              = 16,384 bytes
Chunk 2:  Pixels[32752..49135]              = 16,384 bytes
...
Chunk 46: Pixels[753648..767999]            = 14,352 bytes
```

### Touch Protocol (Interrupt IN)

**Packet Format:**
```cpp
#pragma pack(push, 1)
struct InterruptPacket {
    UINT8 PacketType;       // 0=Status, 1=Touch
    union {
        // Touch event
        struct {
            UINT8 ContactId;    // 0-1
            UINT8 TipSwitch:1;  // 1=touching
            UINT8 InRange:1;    // 1=in range
            UINT8 Reserved:6;   // 0
            UINT16 X;           // 0-799
            UINT16 Y;           // 0-479
        } Touch;                // 6 bytes

        // Status event
        struct {
            UINT32 LastFrameAcked;  // Last frame received
            UINT8 ErrorCode;        // 0=OK
            UINT8 Reserved[3];      // 0
        } Status;                   // 8 bytes
    } Data;
};
#pragma pack(pop)
// Total: 9 bytes (1 type + 8 data)
```

**Example Packets:**

*Touch event (finger down):*
```
01 00 03 20 01 E0 00
│  │  │  │  │  │  └─ (unused in this example)
│  │  │  │  │  └──── Y high byte (480 = 0x01E0)
│  │  │  │  └─────── Y low byte
│  │  │  └────────── X high byte (800 = 0x0320)
│  │  └───────────── X low byte
│  └──────────────── TipSwitch=1, InRange=1, ContactId=0
└─────────────────── PacketType=1 (Touch)
```

*Status event (frame acknowledged):*
```
00 2A 00 00 00 00 00 00 00
│  │           │  └─────────── Reserved
│  │           └────────────── ErrorCode=0
│  └────────────────────────── LastFrameAcked=42 (0x0000002A)
└───────────────────────────── PacketType=0 (Status)
```

---

## Driver Stack

### Windows Driver Stack Layering

```
┌─────────────────────────────────────────────────────────────┐
│  USER MODE                                                   │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────────┐  ┌──────────────────┐                │
│  │ UsbDisplayIdd    │  │ UsbTouchHidUmdf  │                │
│  │ (UMDF 2.33)      │  │ (UMDF 2.33)      │                │
│  └──────────────────┘  └──────────────────┘                │
│           │                     │                            │
│           └──────────┬──────────┘                           │
│                      │                                       │
│           ┌──────────▼──────────┐                           │
│           │ UsbTransportUmdf    │                           │
│           │ (UMDF 2.33)         │                           │
│           └─────────────────────┘                           │
│                      │                                       │
├──────────────────────┼───────────────────────────────────────┤
│  UMDF HOST PROCESS   │                                       │
│  (WUDFHost.exe)      │                                       │
├──────────────────────┼───────────────────────────────────────┤
│                      │ (ALPC/IOCTL)                          │
│  ┌───────────────────▼────────────┐                         │
│  │  UMDF Reflector (WUDFRd.sys)  │                         │
│  └────────────────────────────────┘                         │
│                      │                                       │
├──────────────────────┼───────────────────────────────────────┤
│  KERNEL MODE         │                                       │
├──────────────────────┼───────────────────────────────────────┤
│                      │                                       │
│  ┌───────────────────▼────────────┐                         │
│  │     WinUSB.sys                 │                         │
│  │  (Microsoft USB Stack)         │                         │
│  └────────────────────────────────┘                         │
│                      │                                       │
│  ┌───────────────────▼────────────┐                         │
│  │     USBCCGP.sys                │                         │
│  │  (Composite Device Driver)     │                         │
│  └────────────────────────────────┘                         │
│                      │                                       │
│  ┌───────────────────▼────────────┐                         │
│  │     USB Hub Driver             │                         │
│  └────────────────────────────────┘                         │
│                      │                                       │
│  ┌───────────────────▼────────────┐                         │
│  │  USB Host Controller Driver    │                         │
│  │  (USBXHCI.sys / USBEHCI.sys)   │                         │
│  └────────────────────────────────┘                         │
└──────────────────────────────────────────────────────────────┘
```

### INF File Relationships

```
UsbCompositeExtension.inf
    ↓ (Composite Device USB\VID_1FC9&PID_0094)
    │
    ├─→ UsbTransportUmdf.inf
    │   Service: WinUSB
    │   UmdfService: UsbTransportUmdf
    │   Interface: GUID_DEVINTERFACE_RPUSB_TRANSPORT
    │
    ├─→ UsbDisplayIdd.inf
    │   Compatible ID: UsbDisplay
    │   UmdfService: UsbDisplayIdd
    │   Needs: IddCx framework
    │
    └─→ UsbTouchHid.inf
        Compatible ID: UsbTouch
        UmdfService: UsbTouchHidUmdf
        Needs: HIDClass framework
```

---

## Installation and Deployment

### Prerequisites

**Development Environment:**
- Windows 11 SDK (10.0.22000 or later)
- Windows Driver Kit (WDK) 11
- Visual Studio 2022 (with C++ and WDK extensions)
- .NET Framework 4.8

**Runtime Requirements:**
- Windows 11 Build 22000+
- USB 2.0 High-Speed host controller
- UMDF 2.33 runtime (included in Windows 11)

### Build Process

```batch
REM Navigate to driver directory
cd drivers

REM Build UsbTransportUmdf
cd UsbTransportUmdf
msbuild UsbTransportUmdf.vcxproj /p:Configuration=Release /p:Platform=x64

REM Build UsbDisplayIdd
cd ..\UsbDisplayIdd
msbuild UsbDisplayIdd.vcxproj /p:Configuration=Release /p:Platform=x64

REM Build UsbTouchHidUmdf
cd ..\UsbTouchHidUmdf
msbuild UsbTouchHidUmdf.vcxproj /p:Configuration=Release /p:Platform=x64

REM Sign drivers (test signing)
cd ..\packages
SignTool sign /v /s PrivateCertStore /n DriverTestCert /t http://timestamp.digicert.com *.dll
```

### Installation Steps

**Method 1: Test Signing (Development)**

```batch
REM 1. Enable test signing
bcdedit /set testsigning on
shutdown /r /t 0

REM 2. Install drivers
pnputil /add-driver UsbTransportUmdf.inf /install
pnputil /add-driver UsbDisplayIdd.inf /install
pnputil /add-driver UsbTouchHid.inf /install
pnputil /add-driver UsbCompositeExtension.inf /install

REM 3. Verify installation
pnputil /enum-drivers | findstr "RoboPeak"

REM 4. Connect device and verify
devcon status "USB\VID_1FC9&PID_0094"
```

**Method 2: Attestation Signing (Production)**

```batch
REM 1. Create driver package
cd packages
inf2cat /driver:. /os:10_X64

REM 2. Sign with EV certificate
SignTool sign /v /ac "path\to\crosscert.cer" /s MY /n "Your Company" /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 UsbTransportUmdf.cat

REM 3. Submit to Microsoft Hardware Dev Center
REM    - Upload .cab with drivers + .cat files
REM    - Attestation signing (no HLK required)
REM    - Download signed package

REM 4. Install signed package
pnputil /add-driver UsbTransportUmdf.inf /install
```

### Uninstallation

```batch
REM Remove drivers
pnputil /delete-driver oem123.inf /uninstall
pnputil /delete-driver oem124.inf /uninstall
pnputil /delete-driver oem125.inf /uninstall
pnputil /delete-driver oem126.inf /uninstall

REM Disable test signing (if used)
bcdedit /set testsigning off
```

---

## Development Roadmap

### Phase 1: Core Functionality (Current → Week 2)

**Goal:** Basic display and touch functionality

- [x] USB transport driver structure
- [x] IOCTL interface definition
- [x] USB pipe initialization
- [x] Vendor protocol commands
- [x] IDD driver structure
- [x] EDID generation
- [x] Swap-chain callbacks
- [x] Touch HID descriptor
- [ ] **Implement swap-chain present loop** ⚠️ Critical
- [ ] **Implement USB interrupt handling** ⚠️ Critical
- [ ] **Implement HID input report generation** ⚠️ Critical
- [ ] Basic error handling
- [ ] Frame transmission
- [ ] Touch event processing

### Phase 2: Stability & Performance (Week 3-4)

**Goal:** Production-ready stability

- [ ] Frame chunking for large transfers
- [ ] Asynchronous USB operations
- [ ] Frame skipping under load
- [ ] Device hot-plug handling
- [ ] USB reconnect logic
- [ ] WPP tracing infrastructure
- [ ] Performance profiling
- [ ] Memory leak detection
- [ ] Thread synchronization review

### Phase 3: Advanced Features (Week 5-6)

**Goal:** Enhanced capabilities

- [ ] Multiple display modes (640x480, 1024x600)
- [ ] Dynamic mode switching
- [ ] Power management (D0/D3 transitions)
- [ ] Idle detection
- [ ] Selective suspend
- [ ] 5+ point multitouch (if hardware supports)
- [ ] Pen/stylus support (if hardware supports)
- [ ] Display rotation support

### Phase 4: Testing & Certification (Week 7-10)

**Goal:** WHQL certification

- [ ] Unit tests for each driver
- [ ] Integration tests
- [ ] Stress testing (24hr+ runs)
- [ ] USB analyzer validation
- [ ] HLK test suite execution
  - [ ] Indirect Display tests
  - [ ] Touch/HID tests
  - [ ] USB tests
  - [ ] Power management tests
- [ ] InfVerif validation
- [ ] Static Driver Verifier (SDV)
- [ ] Code Analysis (PREfast)
- [ ] Fix all HLK failures
- [ ] Attestation signing
- [ ] WHQL submission

---

## Testing and Validation

### Unit Testing

**UsbTransportUmdf Tests:**
```cpp
// Test USB initialization
TEST(UsbTransport, DeviceInitialization)
TEST(UsbTransport, PipeConfiguration)
TEST(UsbTransport, VendorCommandPing)
TEST(UsbTransport, VendorCommandGetVersion)
TEST(UsbTransport, FramePush_SmallFrame)
TEST(UsbTransport, FramePush_FullFrame)
TEST(UsbTransport, InterruptRead_TouchData)
TEST(UsbTransport, InterruptRead_StatusData)
TEST(UsbTransport, ErrorHandling_DeviceDisconnect)
```

**UsbDisplayIdd Tests:**
```cpp
TEST(Display, AdapterCreation)
TEST(Display, MonitorArrival)
TEST(Display, EdidParsing)
TEST(Display, ModeEnumeration)
TEST(Display, SwapChainAssignment)
TEST(Display, PixelConversion_BGRA_to_RGB565)
TEST(Display, FrameThrottling)
TEST(Display, DeviceRemoval)
```

**UsbTouchHidUmdf Tests:**
```cpp
TEST(Touch, HidDescriptor)
TEST(Touch, InputReportFormat)
TEST(Touch, SingleTouch_TipDown)
TEST(Touch, SingleTouch_TipUp)
TEST(Touch, MultiTouch_TwoContacts)
TEST(Touch, ContactTracking)
```

### Integration Testing

**End-to-End Display Test:**
```
1. Connect device
2. Verify monitor appears in Display Settings
3. Extend desktop to USB display
4. Move window to USB display
5. Render test patterns:
   - Solid colors (R, G, B, W, K)
   - Gradient patterns
   - Checkerboard
   - Scrolling text
6. Measure frame rate (should be ~60fps)
7. Verify no visual artifacts
```

**End-to-End Touch Test:**
```
1. Open Windows Touch Tester app
2. Perform single touch
3. Verify contact appears at correct location
4. Perform multi-touch (2 fingers)
5. Verify both contacts tracked
6. Test gestures:
   - Pan
   - Zoom (pinch)
   - Rotate
7. Verify touch accuracy (±5 pixels)
```

### HLK Testing

**Required Test Playlists:**
- Display.IndirectDisplay.Manual.xml
- Display.IndirectDisplay.Automated.xml
- Input.Digitizer.Touch.Manual.xml
- Input.Digitizer.Touch.Automated.xml
- Device.DevFund.Reliability.xml
- Device.DevFund.CDA.xml

**Critical Tests:**
- `Test_IndirectDisplay_BasicFunctionality`
- `Test_IndirectDisplay_ModeChange`
- `Test_IndirectDisplay_HotPlug`
- `Test_Touch_ContactAccuracy`
- `Test_Touch_MultiTouch`
- `Test_DevFund_PnP_DisableEnable`
- `Test_DevFund_SurpriseRemoval`

### Debugging Tools

**WinDbg Commands:**
```
!devnode 0 1 USB\VID_1FC9&PID_0094
!devstack <PDO address>
!wdfkd.wdfumdevstacks
!wdfkd.wdfumirps
!usb3kd.usbview
!analyze -v
```

**WPP Tracing:**
```batch
REM Start trace
tracelog -start MyTrace -guid UsbTransportUmdf.guid -f MyTrace.etl

REM Perform actions

REM Stop trace
tracelog -stop MyTrace

REM Parse trace
tracefmt MyTrace.etl -o MyTrace.txt
```

**USB Analyzer:**
- Use Beagle USB 480 or similar
- Capture full enumeration
- Verify frame timing
- Check for USB errors (NAK, STALL)

---

## Performance Considerations

### Display Performance

**Target Metrics:**
- **Frame Rate:** 60 fps (16.67ms frame time)
- **Frame Latency:** <50ms (glass-to-glass)
- **USB Bandwidth:** ~92 MB/s (768KB × 60fps × 2 for overhead)
- **CPU Usage:** <5% on modern CPU (i5/Ryzen 5+)
- **Memory:** <100MB working set per driver

**Optimization Strategies:**

1. **Pixel Conversion:**
   ```cpp
   // SIMD optimization (SSE2/AVX2)
   void ConvertBGRAtoRGB565_SIMD(const uint32_t* bgra, uint16_t* rgb565, size_t count) {
       // Process 8 pixels at a time with AVX2
       __m256i* src = (__m256i*)bgra;
       __m128i* dst = (__m128i*)rgb565;
       // ... SIMD instructions
   }
   ```

2. **Frame Skipping:**
   ```cpp
   // Skip frames if device is behind
   if (stats.FramesSubmitted - stats.FramesAcked > MAX_PENDING_FRAMES) {
       // Drop this frame
       continue;
   }
   ```

3. **Dirty Region Tracking:**
   ```cpp
   // Only transfer changed regions (future enhancement)
   RECT dirtyRect = GetDirtyRegion();
   if (IsEmpty(dirtyRect)) {
       // No changes, skip frame
       continue;
   }
   ```

### Touch Performance

**Target Metrics:**
- **Touch Latency:** <10ms (touch to HID report)
- **Report Rate:** 120 Hz (8.33ms interval)
- **Accuracy:** ±2mm (±5 pixels)

**Optimization:**
- Interrupt endpoint polling at 8ms interval
- Direct memory copy from USB buffer to HID report
- No queuing/buffering of touch events

### USB Performance

**Bandwidth Calculation:**
```
Frame size: 800 × 480 × 2 bytes = 768,000 bytes
Frame rate: 60 fps
Bandwidth: 768,000 × 60 = 46.08 MB/s

USB 2.0 High-Speed theoretical max: 60 MB/s
USB 2.0 practical max: ~40 MB/s

Conclusion: Frame compression or reduced refresh rate may be needed
```

**Strategies:**
1. RLE compression for static content
2. Reduce to 30fps for low-motion scenarios
3. Implement dirty-region updates only
4. Use bulk transfer pipelining (multiple outstanding URBs)

---

## Security Considerations

### UMDF Isolation

**Benefits:**
- Drivers run in user-mode (WUDFHost.exe)
- Crash does not affect kernel
- Limited access to system resources
- Sandboxed execution environment

**Security Features:**
- PnpLockdown=1 in INF (prevent registry modification)
- No direct hardware access
- All USB operations via WinUSB (kernel-mode)
- ALPC communication secured by Windows

### Code Signing

**Development:**
- Self-signed test certificate
- Test signing mode required

**Production:**
- EV Code Signing certificate required
- Attestation signing via Microsoft Hardware Dev Center
- Optional: Full WHQL with HLK testing

### Attack Surface

**Potential Risks:**
1. **Malicious USB device:** Validate all USB descriptors and data
2. **Buffer overflows:** Use safe string functions, validate lengths
3. **Integer overflows:** Check frame dimensions before allocation
4. **DLL injection:** Use code integrity checks

**Mitigations:**
```cpp
// Validate frame header
if (header.Width > MAX_DISPLAY_WIDTH ||
    header.Height > MAX_DISPLAY_HEIGHT ||
    header.PayloadBytes != header.Width * header.Height * 2) {
    return STATUS_INVALID_PARAMETER;
}

// Safe allocation
size_t frameBytes;
if (!NT_SUCCESS(RtlSizeTMult(header.Width, header.Height, &frameBytes)) ||
    !NT_SUCCESS(RtlSizeTMult(frameBytes, 2, &frameBytes))) {
    return STATUS_INTEGER_OVERFLOW;
}
```

---

## Appendix

### A. File Structure

```
rpusbdisp/
├── drivers/
│   ├── UsbTransportUmdf/
│   │   ├── Driver.cpp
│   │   ├── Device.cpp
│   │   ├── Device.h
│   │   ├── Queue.cpp
│   │   ├── Queue.h
│   │   ├── UsbProtocol.h
│   │   ├── UsbIoctl.h
│   │   └── UsbTransportUmdf.vcxproj
│   │
│   ├── UsbDisplayIdd/
│   │   ├── Driver.cpp
│   │   ├── DisplayDevice.cpp
│   │   ├── DisplayDevice.h
│   │   ├── Pipeline.cpp
│   │   ├── Pipeline.h
│   │   ├── Edid.h
│   │   └── UsbDisplayIdd.vcxproj
│   │
│   ├── UsbTouchHidUmdf/
│   │   ├── Driver.cpp
│   │   ├── Device.cpp
│   │   ├── Device.h
│   │   ├── HidReport.h
│   │   └── UsbTouchHidUmdf.vcxproj
│   │
│   └── packages/
│       ├── UsbTransportUmdf.inf
│       ├── UsbDisplayIdd.inf
│       ├── UsbTouchHid.inf
│       └── UsbCompositeExtension.inf
│
├── docs/
│   ├── windows-driver-architecture.md (this file)
│   ├── driver-analysis-report.md
│   └── windows-umdf-driver-plan.md
│
└── tools/
    ├── FramePusher/ (future test tool)
    └── TouchTester/ (future test tool)
```

### B. References

**Microsoft Documentation:**
- [Indirect Display Driver Model](https://docs.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview)
- [IddCx API Reference](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/)
- [UMDF Programming Guide](https://docs.microsoft.com/en-us/windows-hardware/drivers/wdf/getting-started-with-umdf-version-2)
- [USB Driver Development](https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/)
- [HID Minidriver](https://docs.microsoft.com/en-us/windows-hardware/drivers/hid/minidrivers-and-the-hid-class-driver)

**Sample Drivers:**
- [Windows-driver-samples/video/IndirectDisplay](https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay)
- [Windows-driver-samples/usb/umdf2\_fx2](https://github.com/microsoft/Windows-driver-samples/tree/main/usb/umdf2_fx2)
- [Windows-driver-samples/hid/vhidmini2](https://github.com/microsoft/Windows-driver-samples/tree/main/hid/vhidmini2)

**Tools:**
- [Windows Driver Kit (WDK)](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)
- [Hardware Lab Kit (HLK)](https://docs.microsoft.com/en-us/windows-hardware/test/hlk/)
- [USBPcap](https://desowin.org/usbpcap/) - USB packet capture
- [Beagle USB Analyzer](https://www.totalphase.com/products/beagle-usb480/)

### C. Glossary

| Term | Definition |
|------|------------|
| **IddCx** | Indirect Display Class Extension - Microsoft framework for virtual/USB displays |
| **UMDF** | User-Mode Driver Framework - allows drivers to run in user space |
| **WDF** | Windows Driver Framework - unified driver model |
| **EDID** | Extended Display Identification Data - monitor metadata |
| **HID** | Human Interface Device - standard for input devices |
| **PTP** | Precision Touchpad Protocol - Windows touch protocol |
| **DXGI** | DirectX Graphics Infrastructure - low-level graphics API |
| **ALPC** | Advanced Local Procedure Call - IPC mechanism |
| **WPP** | Windows software trace PreProcessor - logging framework |
| **HLK** | Hardware Lab Kit - Microsoft certification tests |
| **WHQL** | Windows Hardware Quality Labs - certification program |

---

**Document Status:** Draft
**Last Updated:** 2025-11-19
**Next Review:** After Phase 1 completion
**Owner:** RoboPeak Driver Development Team
