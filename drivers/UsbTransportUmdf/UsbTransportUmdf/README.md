# UsbTransportUmdf

This folder hosts the Windows 11 UMDF v2 USB transport driver that bridges the RoboPeak display hardware with higher level display and touch stacks.  The driver is modeled after the Visual Studio "User Mode Driver, USB (UMDF V2)" template but tailored to the proprietary bulk protocol documented in `docs/windows-umdf-driver-plan.md` and the Linux driver.

Key responsibilities:

- Discover the RoboPeak USB device (default VID `0x1FC9`, PID `0x0094`) and prepare its configuration/pipe handles.
- Provide IOCTLs (see `UsbIoctl.h`) that higher level components such as the IddCx indirect display driver use to push frames, query firmware state, and monitor device health.
- Host a continuous reader for the interrupt endpoint so the transport can surface acknowledgments, touch packets, or other vendor specific notifications.
- Marshal trace logging through WPP to aid HLK and manual debugging sessions.

The implementation follows the standard UMDF patterns: `Driver.cpp` wires up the `EvtDeviceAdd` callback, `Device.cpp` owns hardware preparation/teardown, and `Queue.cpp` implements the IOCTL contract.  The driver is intentionally header-only (no Visual Studio project) so it can be imported into a WDK solution while still being readable from non-Windows development hosts.
