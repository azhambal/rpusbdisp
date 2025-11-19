# UsbTouchHidUmdf

`UsbTouchHidUmdf` is a Windows 11 UMDF v2 HID mini-driver that publishes the RoboPeak digitizer as a standard multitouch device.  It is modeled after the `vhidmini2` sample and is intended to sit on top of the same USB transport driver used by the display stack.

Highlights:

- `Driver.cpp` contains the UMDF entry point and queue setup.
- `Device.*` translates vendor packets into HID input reports and exposes feature reports for calibration/state.
- `HidReport.h` defines the report descriptor that advertises two concurrent contacts with absolute coordinates.

The implementation is a functional skeleton—`Device.cpp` contains TODO markers for wiring the USB interrupt reader and generating HID reports.  This keeps the file readable from Linux/macOS hosts while making it trivial to copy into a Visual Studio UMDF project on Windows.
