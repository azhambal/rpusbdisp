# UsbDisplayIdd

`UsbDisplayIdd` is the UMDF 2 indirect display driver (IddCx) that consumes the USB transport IOCTL contract defined in `../UsbTransportUmdf`.  The driver advertises an EDID, exposes monitor modes, accepts swap-chain assignments, and pushes completed frames back to the transport.

The code is intentionally split into small translation units to keep the responsibilities clear:

- `Driver.cpp` wires up WDF/IddCx initialization.
- `DisplayDevice.*` owns IddCx adapters/monitors and the device lifecycle.
- `Pipeline.*` hosts the DXGI surface conversion pipeline and marshals IOCTLs to the USB transport driver, including converting
  BGRA swap-chain surfaces into RGB565 USB frame payloads.
- `Edid.h` contains a static 800x480 EDID blob that can be extended later.

The implementation is derived from the official Microsoft [IddSampleDriver](https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay) but trimmed down so it can live alongside the rest of the RoboPeak sources.  It currently focuses on scaffolding: the swap-chain, format conversion, and throttling hooks are implemented as TODOs with trace logging so follow-up changes can flesh them out incrementally.
