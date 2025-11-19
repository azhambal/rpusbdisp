# Windows UMDF USB Display and Touch Driver Plan

This document captures a practical, end-to-end plan for creating Windows 11 user-mode drivers for the RoboPeak USB display hardware. It complements the existing Linux kernel driver found in `drivers/linux-driver/` and reuses its USB transport knowledge while targeting the Windows UMDF stack.

## Goals
- Expose a USB-connected framebuffer-like device as a Windows monitor through the Indirect Display Driver (IddCx) model.
- Provide multitouch input by presenting standard HID touch reports via a UMDF HID mini-driver.

## Proposed Solution Layout
```
/drivers
  /UsbTransportUmdf        # UMDF USB client driver (pipes, reader, IOCTLs)
  /UsbDisplayIdd           # UMDF IddCx-based indirect display driver
  /UsbTouchHidUmdf         # UMDF HID mini-driver for multitouch
  /packages
    UsbDisplayIdd.inf
    UsbTouchHid.inf
    UsbCompositeExtension.inf (optional, for a composite device)
/tools
  FramePusher.exe          # sends test frames over USB
  TouchTester.exe          # visualizes multitouch contacts
```

## Phase Overview
1. **USB Transport (UMDF USB client driver)**
   - Start from the Visual Studio "User Mode Driver, USB (UMDF v2)" template.
   - Enumerate the RoboPeak device (VID/PID), select configuration, and cache bulk/interrupt pipes.
   - Configure a continuous reader for the interrupt/status endpoint.
   - Define a vendor protocol (init, mode set, blit, ping) and expose IOCTLs for higher layers.

2. **Display Path via IddCx**
   - Base on the Microsoft IddCx sample driver.
   - Advertise monitor EDID/modes, handle swap-chain assignment, and convert DXGI surfaces (e.g., BGRA8 → RGB565) before USB transfer.
   - Chunk frame data to match USB MTU and throttle using device acknowledgments.
   - Handle hot-unplug, power transitions, and back-pressure.

3. **Touch Input via UMDF HID**
   - Start from the vhidmini2 UMDF sample as a HID mini-driver skeleton.
   - Provide a multitouch digitizer report descriptor (Usage Page 0x0D, Touch Screen 0x04) with required fields: ContactID, TipSwitch, InRange, X, Y, Contact Count/Max.
   - Translate vendor packets into standard HID input reports.

## Build, Signing, and Installation
- Develop on Windows 11 with Visual Studio 2022 plus the latest WDK/SDK.
- Enable test signing initially (`bcdedit /set testsigning on`) or use a test certificate.
- Install using `pnputil /add-driver UsbDisplayIdd.inf /install` and `pnputil /add-driver UsbTouchHid.inf /install`.
- For release, plan on attestation signing or HLK-based WHCP submission.

## HLK and Reliability
- Run HLK playlists for Indirect Display Driver basics and Windows Touch tests.
- Validate INF files with InfVerif and confirm UMDF directives (UmdfLibraryVersion, UmdfService, UmdfServiceOrder, reflector binding).
- Implement robust unplug/resume handling: cancel pending transfers, reinitialize pipes, and ensure WPP/ETW tracing is enabled for diagnostics.

## Reusing Linux Driver Insights
- The existing Linux driver under `drivers/linux-driver/` shows USB command and framebuffer behavior that can inform the Windows transport and pixel-format expectations.
- Mirror the Linux driver's vendor protocol (init/version queries, frame pushes) to reduce firmware changes where possible.
