# Unit Test Plan for Windows UMDF Drivers

**Document Version:** 1.0
**Date:** 2025-11-19
**Status:** Draft
**Test Framework:** TAEF (Test Authoring and Execution Framework)

---

## Overview

This document outlines the unit testing strategy for the RoboPeak USB Display UMDF drivers. The test suite covers the three main driver components and validates Milestone 2 features.

## Test Framework Selection

**Primary Framework: TAEF (Test Authoring and Execution Framework)**
- Part of Windows Driver Kit (WDK)
- Supports kernel-mode and user-mode testing
- Integrates with Visual Studio and HLK
- Provides data-driven testing capabilities

**Alternative: WDTF (Windows Driver Test Framework)**
- For integration and system-level tests
- Used for device installation and PnP testing

---

## Test Categories

### 1. UsbTransportUmdf Driver Tests

#### 1.1 Device Initialization Tests

**Test Class:** `UsbTransportDeviceInitTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestDeviceEnumeration` | Verify device enumerates with correct VID/PID | Device object created, STATUS_SUCCESS |
| `TestUsbInterfaceSelection` | Verify USB interface 0 is selected | Interface configured correctly |
| `TestBulkPipeConfiguration` | Verify Bulk IN/OUT pipes are configured | Pipes created with correct endpoints |
| `TestInterruptPipeConfiguration` | Verify Interrupt IN pipe is configured | Pipe created with 64-byte buffer |
| `TestDeviceInterfaceCreation` | Verify GUID_DEVINTERFACE_RPUSB_TRANSPORT | Device interface published |
| `TestTouchDataBufferInit` | Verify touch data buffer initialization | Spinlock created, event initialized |

#### 1.2 IOCTL Handler Tests

**Test Class:** `UsbTransportIoctlTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestIoctlPing` | Send IOCTL_RPUSB_PING | Returns STATUS_SUCCESS |
| `TestIoctlGetVersion` | Send IOCTL_RPUSB_GET_VERSION | Returns firmware version |
| `TestIoctlSetMode` | Send IOCTL_RPUSB_SET_MODE with mode=0 | Vendor control request sent |
| `TestIoctlPushFrameChunk` | Send IOCTL_RPUSB_PUSH_FRAME_CHUNK | Chunk transmitted via bulk OUT |
| `TestIoctlInvalidBuffer` | Send IOCTL with invalid buffer size | Returns STATUS_BUFFER_TOO_SMALL |
| `TestIoctlGetTouchData` | Send IOCTL_RPUSB_GET_TOUCH_DATA | Returns latest touch contacts |
| `TestIoctlGetStatistics` | Send IOCTL_RPUSB_GET_STATISTICS | Returns frame counters |

#### 1.3 Frame Chunking Tests (Milestone 2)

**Test Class:** `UsbTransportChunkingTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestChunkSizeCalculation` | Verify 768KB frame → 47 chunks | totalChunks = 47 |
| `TestChunkHeaderPopulation` | Verify chunk header fields | FrameId, ChunkIndex, TotalChunks correct |
| `TestChunkBoundaryConditions` | Test first and last chunks | Correct offsets and sizes |
| `TestChunkDataIntegrity` | Verify chunk data matches source | RtlCompareMemory returns 0 |
| `TestPartialFrameHandling` | Send incomplete frame | Previous chunks discarded |
| `TestFrameIdGeneration` | Verify unique frame IDs | InterlockedIncrement behavior |

#### 1.4 Reconnect Logic Tests (Milestone 2)

**Test Class:** `UsbTransportReconnectTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestExponentialBackoff` | Verify retry delays: 100ms, 200ms, 400ms | Timing within ±10ms |
| `TestRetryCountLimit` | Verify max 3 retries (4 attempts total) | Stops after 4th attempt |
| `TestSuccessfulRetry` | Simulate failure then success | Returns STATUS_SUCCESS on retry 2 |
| `TestPermanentFailure` | Simulate continuous failures | Returns error after max retries |
| `TestDeviceRemovalDuringRetry` | Disconnect device mid-retry | Graceful termination |

#### 1.5 WPP Tracing Tests (Milestone 2)

**Test Class:** `UsbTransportTracingTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestTraceProviderRegistration` | Verify WPP provider registered | ETW session can subscribe |
| `TestTraceLevels` | Test ERROR, WARNING, INFO, VERBOSE | Correct filtering by level |
| `TestTraceFlags` | Verify 6 trace flags work | TRACE_DRIVER, TRACE_USB, etc. |
| `TestTraceMessageFormatting` | Verify %!STATUS! formatting | Human-readable status codes |

#### 1.6 Power Management Tests (Milestone 3)

**Test Class:** `UsbTransportPowerTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestD0Entry` | Transition from D3 → D0 | Interrupt pipe restarted |
| `TestD0Exit` | Transition from D0 → D3 | Interrupt pipe stopped, DeviceReady=FALSE |
| `TestTouchBufferClearOnD3` | Verify touch data cleared in D3 | ContactCount=0 |
| `TestD0D3Cycle` | Full cycle: D0 → D3 → D0 | Device operational after resume |

---

### 2. UsbDisplayIdd Driver Tests

#### 2.1 IddCx Adapter Tests

**Test Class:** `DisplayAdapterTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestAdapterCreation` | Verify IddCx adapter initialized | Adapter handle valid |
| `TestMonitorArrival` | Verify monitor object created | Monitor added to system |
| `TestEdidParsing` | Verify EDID data validity | Checksum correct, 128 bytes |

#### 2.2 Display Mode Tests (Milestone 3)

**Test Class:** `DisplayModeTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestModeEnumeration` | Verify 3 modes registered | 640x480, 800x480, 1024x600 |
| `TestDefaultMode` | Verify default mode is 800x480 | DefaultMonitorModeIndex = 1 |
| `TestModeSwitch640x480` | Switch to 640x480 | CurrentWidth=640, CurrentHeight=480 |
| `TestModeSwitch1024x600` | Switch to 1024x600 | CurrentWidth=1024, CurrentHeight=600 |
| `TestCommitModesCallback` | Verify EvtAdapterCommitModes called | Context updated with new mode |

#### 2.3 Swap-Chain Tests

**Test Class:** `DisplaySwapChainTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestSwapChainAssignment` | Verify swap-chain assigned | PresentThread created |
| `TestPresentLoopStart` | Verify present loop running | Thread state = Running |
| `TestSwapChainUnassignment` | Verify graceful thread termination | ShouldStop=TRUE, thread exits |
| `TestMultipleAssignUnassign` | Cycle assign/unassign 10 times | No resource leaks |

#### 2.4 Pixel Conversion Tests

**Test Class:** `DisplayPixelConversionTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestBgraToRgb565White` | Convert #FFFFFFFF (white) | RGB565 = 0xFFFF |
| `TestBgraToRgb565Red` | Convert #FF0000FF (red) | RGB565 = 0xF800 |
| `TestBgraToRgb565Green` | Convert #FF00FF00 (green) | RGB565 = 0x07E0 |
| `TestBgraToRgb565Blue` | Convert #FFFF0000 (blue) | RGB565 = 0x001F |
| `TestBgraToRgb565Black` | Convert #FF000000 (black) | RGB565 = 0x0000 |
| `TestBgraToRgb565Performance` | Convert 800x480 frame | Time < 5ms |

#### 2.5 Frame Chunking Integration Tests

**Test Class:** `DisplayFrameChunkingTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestChunkGenerationFor640x480` | 614,400 bytes → 38 chunks | Correct chunk count |
| `TestChunkGenerationFor800x480` | 768,000 bytes → 47 chunks | Correct chunk count |
| `TestChunkGenerationFor1024x600` | 1,228,800 bytes → 76 chunks | Correct chunk count |

#### 2.6 Power Management Tests (Milestone 3)

**Test Class:** `DisplayPowerTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestD0Entry` | Resume from D3 | Display operations resume |
| `TestD0Exit` | Suspend to D3 | Present loop pauses gracefully |
| `TestPresentDuringD3` | Attempt present in D3 | No crash, graceful degradation |

---

### 3. UsbTouchHidUmdf Driver Tests

#### 3.1 HID Descriptor Tests

**Test Class:** `TouchHidDescriptorTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestHidDeviceDescriptor` | Get HID device descriptor | Version=0x0100, ReportLength correct |
| `TestHidReportDescriptor` | Get HID report descriptor | Valid Usage Page 0x0D |
| `TestHidDeviceAttributes` | Get device attributes | VID=0x1FC9, PID=0x0094 |
| `TestMaxContactCount` | Verify ContactMax feature | Returns 2 contacts |

#### 3.2 Touch Input Report Tests

**Test Class:** `TouchInputReportTests`

| Test Case | Description | Expected Result |
|-----------|-------------|-----------------|
| `TestSingleTouchDown` | Generate report for 1 contact | TipSwitch=1, ContactCount=1 |
| `TestSingleTouchUp` | Generate report for release | TipSwitch=0, InRange=0 |
| `TestMultiTouch2Contacts` | Generate report for 2 contacts | ContactCount=2, both contacts valid |
| `TestContactIdTracking` | Verify ContactID 0 and 1 | IDs unique and stable |
| `TestCoordinateRange` | Test X=0,799 Y=0,479 | Values within logical range |
| `TestNoTouch` | Generate report with no contacts | ContactCount=0 |

---

## Test Execution Strategy

### Test Phases

**Phase 1: Unit Tests (Current)**
- Run on development machine with test signing
- Execute via Visual Studio Test Explorer
- Target: 100% pass rate before Milestone 2 completion

**Phase 2: Integration Tests**
- Run on physical hardware with RoboPeak device
- Verify end-to-end data flow
- Target: Basic functionality validated

**Phase 3: HLK Tests**
- Run full Hardware Lab Kit test suite
- Target: WHQL certification readiness

### Test Execution Commands

```batch
REM Build test binaries
msbuild /p:Configuration=Test /p:Platform=x64 UsbTransportUmdfTests.vcxproj

REM Run all tests
te.exe UsbTransportUmdfTests.dll

REM Run specific test class
te.exe UsbTransportUmdfTests.dll /name:UsbTransportChunkingTests::*

REM Run with WPP tracing
te.exe UsbTransportUmdfTests.dll /enablewtt

REM Generate coverage report
te.exe UsbTransportUmdfTests.dll /coverage
```

### Continuous Integration

**GitHub Actions Workflow:**
```yaml
name: Driver Unit Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v3
      - name: Setup WDK
        run: |
          choco install windows-driver-kit
      - name: Build Tests
        run: msbuild /p:Configuration=Test /p:Platform=x64
      - name: Run Unit Tests
        run: te.exe **/*Tests.dll /inproc
      - name: Upload Results
        uses: actions/upload-artifact@v3
        with:
          name: test-results
          path: TestResults/
```

---

## Test Coverage Metrics

### Target Coverage

| Component | Line Coverage | Branch Coverage | Function Coverage |
|-----------|---------------|-----------------|-------------------|
| UsbTransportUmdf | ≥80% | ≥75% | ≥90% |
| UsbDisplayIdd | ≥80% | ≥75% | ≥90% |
| UsbTouchHidUmdf | ≥85% | ≥80% | ≥95% |

### Coverage Exclusions

- WPP tracing macros
- Error handling paths that require device failure
- Hardware-specific timing edge cases

---

## Test Infrastructure

### Mock Objects

**MockUsbDevice**
- Simulates USB device enumeration
- Provides fake VID/PID
- Returns canned descriptor data

**MockIddCxFramework**
- Simulates IddCx callbacks
- Provides fake swap-chain handles
- Allows testing without graphics stack

**MockTouchDigitizer**
- Simulates touch events
- Generates interrupt packets
- Tests contact tracking logic

### Test Fixtures

```cpp
class UsbTransportTestFixture
{
public:
    NTSTATUS SetUp()
    {
        // Create mock device
        // Initialize test environment
        return STATUS_SUCCESS;
    }

    NTSTATUS TearDown()
    {
        // Clean up resources
        // Verify no memory leaks
        return STATUS_SUCCESS;
    }

    WDFDEVICE GetMockDevice() { return m_device; }

private:
    WDFDEVICE m_device;
    MockUsbDevice m_mockUsb;
};
```

---

## Test Implementation Status

### Milestone 2 Tests

| Category | Tests Defined | Tests Implemented | Pass Rate |
|----------|---------------|-------------------|-----------|
| Frame Chunking | 6 | 0 | - |
| Reconnect Logic | 5 | 0 | - |
| WPP Tracing | 4 | 0 | - |
| **Total** | **15** | **0** | **0%** |

### Milestone 3 Tests

| Category | Tests Defined | Tests Implemented | Pass Rate |
|----------|---------------|-------------------|-----------|
| Display Modes | 5 | 0 | - |
| Power Management | 7 | 0 | - |
| **Total** | **12** | **0** | **0%** |

---

## Next Steps

1. **Create Test Project Structure** (Week 1)
   - Set up TAEF test projects for each driver
   - Configure Visual Studio test runner
   - Implement mock objects and test fixtures

2. **Implement Milestone 2 Tests** (Week 2)
   - Frame chunking validation tests
   - Reconnect logic timing tests
   - WPP tracing verification tests

3. **Implement Milestone 3 Tests** (Week 3)
   - Display mode switching tests
   - Power management state tests

4. **Integration Testing** (Week 4)
   - End-to-end tests with real hardware
   - Performance benchmarking
   - Stress testing (24+ hours)

5. **HLK Preparation** (Week 5+)
   - Run HLK test playlists
   - Fix failures
   - Document test results

---

## References

- [TAEF Documentation](https://docs.microsoft.com/en-us/windows-hardware/drivers/taef/)
- [WDK Testing Guide](https://docs.microsoft.com/en-us/windows-hardware/drivers/develop/testing-a-driver)
- [Driver Verifier](https://docs.microsoft.com/en-us/windows-hardware/drivers/devtest/driver-verifier)
- [Code Coverage Tools](https://docs.microsoft.com/en-us/visualstudio/test/using-code-coverage-to-determine-how-much-code-is-being-tested)

---

**Document Owner:** RoboPeak Driver Development Team
**Last Updated:** 2025-11-19
**Next Review:** After Milestone 2 test implementation
