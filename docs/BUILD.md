# Build Guide: RoboPeak USB Display Windows Drivers

**Complete step-by-step guide to compile and deploy Windows UMDF drivers**

---

## Table of Contents
1. [Prerequisites](#prerequisites)
2. [Environment Setup](#environment-setup)
3. [Project Structure](#project-structure)
4. [Building the Drivers](#building-the-drivers)
5. [Signing the Drivers](#signing-the-drivers)
6. [Installing the Drivers](#installing-the-drivers)
7. [Testing and Debugging](#testing-and-debugging)
8. [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Operating System
- **Windows 11** (Build 22000 or later) **REQUIRED**
  - Windows 10 21H2+ may work but is not tested
  - Driver development requires modern WDK features

### Development Tools

#### 1. Visual Studio 2022 (17.0 or later)
**Download:** https://visualstudio.microsoft.com/downloads/

**Required Workloads:**
- ✅ Desktop development with C++
- ✅ Windows application development

**Required Components:**
- MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)
- Windows 11 SDK (10.0.22621.0 or later)
- C++ ATL for latest build tools (x86 & x64)
- C++ MFC for latest build tools (x86 & x64)

**Installation Command (from Administrator PowerShell):**
```powershell
# Download Visual Studio installer, then run:
vs_community.exe --add Microsoft.VisualStudio.Workload.NativeDesktop `
                 --add Microsoft.VisualStudio.Workload.Universal `
                 --add Microsoft.VisualStudio.Component.VC.ATL `
                 --add Microsoft.VisualStudio.Component.VC.ATLMFC `
                 --add Microsoft.VisualStudio.Component.Windows11SDK.22621
```

#### 2. Windows Driver Kit (WDK) 11
**Download:** https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk

**Version Required:** WDK for Windows 11, version 22H2 (10.0.22621.x)

**Installation Steps:**
1. Download `wdksetup.exe` from Microsoft
2. Run installer as Administrator
3. Select "Install Windows Driver Kit" (full installation)
4. Wait for completion (may take 15-30 minutes)

**Verify Installation:**
```powershell
# Check WDK installation
dir "C:\Program Files (x86)\Windows Kits\10\Include\wdf"
# Should show KMDF and UMDF directories
```

#### 3. Windows SDK 11
**Usually installed with Visual Studio**, but if needed separately:
**Download:** https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/

**Version Required:** 10.0.22621.0 or later

#### 4. Spectre-mitigated Libraries (Optional but Recommended)
**Required for production drivers:**
```powershell
# Install via Visual Studio Installer
# Add component: "MSVC v143 - VS 2022 C++ x64/x86 Spectre-mitigated libs (Latest)"
```

### Hardware Requirements
- **CPU:** x64 processor (ARM64 not currently supported)
- **RAM:** 16 GB minimum (32 GB recommended for debugging)
- **Disk:** 50 GB free space for tools and symbols
- **USB:** USB 2.0 or 3.0 port for testing RoboPeak device

---

## Environment Setup

### 1. Enable Test Signing (REQUIRED for development)

**⚠️ WARNING:** Test signing reduces system security. Only enable on development machines.

```powershell
# Run as Administrator
bcdedit /set testsigning on

# Verify
bcdedit /enum {current}
# Should show: testsigning = Yes
```

**Reboot required after enabling test signing.**

### 2. Install WDK Visual Studio Extension

**Verify WDK Extension is loaded:**
1. Open Visual Studio 2022
2. Navigate to **Extensions → Manage Extensions**
3. Search for "Windows Driver Kit"
4. Ensure "WDK for Windows 11" is installed and enabled

If not installed:
1. Close Visual Studio
2. Run WDK installer again
3. Select "Install Visual Studio Extension"

### 3. Configure Debugging Tools

**Install WinDbg Preview:**
```powershell
# Via Microsoft Store or winget
winget install Microsoft.WinDbg
```

**Configure Symbol Path:**
```powershell
# Set environment variable (system-wide)
setx /M _NT_SYMBOL_PATH "SRV*C:\Symbols*https://msdl.microsoft.com/download/symbols"
```

### 4. Set Up Driver Development Environment Variables

```powershell
# Add to system environment (optional, for command-line builds)
setx /M WDK_ROOT "C:\Program Files (x86)\Windows Kits\10"
setx /M SDK_ROOT "C:\Program Files (x86)\Windows Kits\10"
```

---

## Project Structure

### Repository Layout
```
rpusbdisp/
├── drivers/
│   ├── UsbTransportUmdf/      # USB transport driver (UMDF 2.0)
│   │   ├── Device.cpp          # Device callbacks & USB pipe setup
│   │   ├── Device.h            # Device context structures
│   │   ├── Queue.cpp           # IOCTL handlers
│   │   ├── Queue.h
│   │   ├── Driver.cpp          # DriverEntry & initialization
│   │   ├── UsbIoctl.h          # IOCTL definitions
│   │   ├── UsbProtocol.h       # USB protocol structures
│   │   ├── Trace.h             # WPP tracing macros
│   │   ├── UsbTransportUmdf.rc # Resource file
│   │   └── UsbTransportUmdf.inf # INF installation file
│   │
│   ├── UsbDisplayIdd/          # Indirect Display Driver (IddCx)
│   │   ├── DisplayDevice.cpp   # IddCx adapter & monitor
│   │   ├── DisplayDevice.h
│   │   ├── Driver.cpp          # DriverEntry
│   │   ├── Pipeline.cpp        # Frame processing pipeline
│   │   ├── Pipeline.h
│   │   ├── Edid.h              # EDID data (320x240)
│   │   ├── Trace.h             # WPP tracing macros
│   │   ├── UsbDisplayIdd.rc
│   │   └── UsbDisplayIdd.inf
│   │
│   ├── UsbTouchHidUmdf/        # HID Touch driver (UMDF 2.0)
│   │   ├── Device.cpp          # HID minidriver implementation
│   │   ├── Device.h
│   │   ├── Driver.cpp
│   │   ├── HidReport.h         # HID report descriptors
│   │   ├── Trace.h
│   │   ├── UsbTouchHidUmdf.rc
│   │   └── UsbTouchHidUmdf.inf
│   │
│   └── common/
│       └── inc/
│           └── protocol.h      # Shared protocol definitions
│
├── docs/                       # Documentation
└── build/                      # Build output (created during build)
```

### Creating Visual Studio Projects

Since the repository contains source files but not Visual Studio project files, you need to create them:

#### Option 1: Use WDK Templates (Recommended)

**For UsbTransportUmdf:**
1. Open Visual Studio 2022
2. **File → New → Project**
3. Search for "User Mode Driver, USB (UMDF V2)"
4. Name: `UsbTransportUmdf`
5. Location: `C:\path\to\rpusbdisp\drivers\`
6. Click **Create**
7. **Delete template files**, replace with repository files
8. **Add existing items** from `drivers/UsbTransportUmdf/`

**For UsbDisplayIdd:**
1. **File → New → Project**
2. Search for "Kernel Mode Driver, Empty (KMDF)"
3. Name: `UsbDisplayIdd`
4. **IMPORTANT:** Change to UMDF after creation
5. Add IddCx references (see below)

**For UsbTouchHidUmdf:**
1. **File → New → Project**
2. Search for "User Mode Driver, Empty (UMDF V2)"
3. Name: `UsbTouchHidUmdf`
4. Add HID minidriver references

#### Option 2: Manual Project Creation

**Create Solution File:**
```
rpusbdisp.sln  (in drivers/ directory)
```

**Project Structure (for each driver):**
- Project Type: `Driver (Universal Windows)`
- Platform Toolset: `WindowsUserModeDriver10.0`
- Configuration Type: `Driver`
- Target Platform: `Universal`

---

## Building the Drivers

### Method 1: Visual Studio GUI (Easiest)

#### Step 1: Open Solution
```
1. Launch Visual Studio 2022
2. File → Open → Project/Solution
3. Navigate to: drivers/rpusbdisp.sln
4. Click Open
```

#### Step 2: Configure Projects

**For UsbTransportUmdf:**
```
Project Properties → Configuration Properties → Driver Settings
├── Target OS Version: Windows 11
├── Target Platform: Universal
└── Minimum UMDF Version: 2.31

Configuration Properties → C/C++ → General
├── Additional Include Directories:
│   $(ProjectDir);
│   $(ProjectDir)..\common\inc;
│   %(AdditionalIncludeDirectories)

Configuration Properties → Linker → Input
└── Additional Dependencies:
    OneCoreUAP.lib
    $(DDK_LIB_PATH)wdfdrivers.lib
    $(SDK_LIB_PATH)\um\x64\uuid.lib
```

**For UsbDisplayIdd:**
```
Configuration Properties → Driver Settings
├── Target OS Version: Windows 11
├── Target Platform: Universal
└── Use IddCx: Yes

Configuration Properties → C/C++ → General
├── Additional Include Directories:
│   $(ProjectDir);
│   $(ProjectDir)..\common\inc;
│   $(KIT_ROOT)\Include\$(SDK_VERSION)\um;
│   %(AdditionalIncludeDirectories)

Configuration Properties → Linker → Input
└── Additional Dependencies:
    OneCoreUAP.lib
    $(DDK_LIB_PATH)IddCx.lib
    $(DDK_LIB_PATH)wdfdrivers.lib
    d3d11.lib
    dxgi.lib
```

**For UsbTouchHidUmdf:**
```
Configuration Properties → Driver Settings
├── Target OS Version: Windows 11
├── Target Platform: Universal
└── Minimum UMDF Version: 2.31

Configuration Properties → Linker → Input
└── Additional Dependencies:
    OneCoreUAP.lib
    $(DDK_LIB_PATH)wdfdrivers.lib
    $(DDK_LIB_PATH)hidclass.lib
    $(DDK_LIB_PATH)mshidumdf.lib
```

#### Step 3: Enable WPP Tracing

**For ALL drivers:**
```
Configuration Properties → WPP Tracing → All Options
├── Run WPP Tracing: Yes
├── Scan Configuration Data: Trace.h
└── Function to generate trace messages:
    DoTraceMessage(LEVEL,FLAGS,MSG,...)
```

#### Step 4: Build Configuration

**Select Build Configuration:**
```
Toolbar: Configuration Manager
├── Active Solution Configuration: Debug or Release
├── Active Solution Platform: x64 (ARM64 not supported)
└── Configuration: Debug x64
```

**Build Order:**
1. UsbTransportUmdf (build first - no dependencies)
2. UsbDisplayIdd (depends on protocol headers)
3. UsbTouchHidUmdf (depends on protocol headers)

#### Step 5: Build

**Build All:**
```
Build → Build Solution (Ctrl+Shift+B)
```

**Or build individually:**
```
Right-click on project → Build
```

**Expected Output:**
```
drivers\UsbTransportUmdf\x64\Debug\
├── UsbTransportUmdf.dll        # Driver binary
├── UsbTransportUmdf.inf        # Installation file
├── UsbTransportUmdf.pdb        # Debug symbols
└── UsbTransportUmdf.cat        # Catalog file (if signed)

drivers\UsbDisplayIdd\x64\Debug\
├── UsbDisplayIdd.dll
├── UsbDisplayIdd.inf
├── UsbDisplayIdd.pdb
└── UsbDisplayIdd.cat

drivers\UsbTouchHidUmdf\x64\Debug\
├── UsbTouchHidUmdf.dll
├── UsbTouchHidUmdf.inf
├── UsbTouchHidUmdf.pdb
└── UsbTouchHidUmdf.cat
```

### Method 2: Command Line (MSBuild)

#### Step 1: Open Developer Command Prompt
```
Start → Visual Studio 2022 → Developer Command Prompt for VS 2022
```

#### Step 2: Navigate to Repository
```cmd
cd C:\path\to\rpusbdisp\drivers
```

#### Step 3: Build with MSBuild
```cmd
REM Build all projects in solution
msbuild rpusbdisp.sln /p:Configuration=Debug /p:Platform=x64 /t:Build

REM Or build individual project
msbuild UsbTransportUmdf\UsbTransportUmdf.vcxproj /p:Configuration=Debug /p:Platform=x64

REM Build Release version
msbuild rpusbdisp.sln /p:Configuration=Release /p:Platform=x64 /t:Build

REM Rebuild all (clean + build)
msbuild rpusbdisp.sln /p:Configuration=Debug /p:Platform=x64 /t:Rebuild

REM Parallel build (faster)
msbuild rpusbdisp.sln /p:Configuration=Debug /p:Platform=x64 /m:4
```

#### Step 4: Verify Build Output
```cmd
dir /s *.dll
dir /s *.inf
dir /s *.pdb
```

### Method 3: Using stampinf and inf2cat (Manual)

**If INF files need manual processing:**

```cmd
REM Process INF file (stamp with version/date)
stampinf -f UsbTransportUmdf.inf -d * -v * -a x64 -c UsbTransportUmdf.cat

REM Create catalog file
inf2cat /driver:. /os:10_X64,10_AU_X64,10_RS2_X64,10_RS3_X64,10_RS4_X64,Server10_X64
```

---

## Signing the Drivers

### Development Signing (Test Certificate)

#### Step 1: Create Test Certificate

**Only do this ONCE per development machine:**

```powershell
# Run as Administrator
cd C:\path\to\rpusbdisp\drivers

# Create self-signed certificate
makecert -r -pe -ss PrivateCertStore -n "CN=RoboPeak Test Cert" TestCert.cer

# Install certificate to Trusted Root and Trusted Publishers
certmgr /add TestCert.cer /s /r localMachine root
certmgr /add TestCert.cer /s /r localMachine trustedpublisher
```

#### Step 2: Sign Each Driver

**Sign UsbTransportUmdf:**
```cmd
cd UsbTransportUmdf\x64\Debug

REM Create catalog from INF
inf2cat /driver:. /os:10_X64

REM Sign catalog file
signtool sign /v /s PrivateCertStore /n "RoboPeak Test Cert" /t http://timestamp.digicert.com UsbTransportUmdf.cat

REM Verify signature
signtool verify /pa /v UsbTransportUmdf.cat
```

**Sign UsbDisplayIdd:**
```cmd
cd ..\..\UsbDisplayIdd\x64\Debug

inf2cat /driver:. /os:10_X64
signtool sign /v /s PrivateCertStore /n "RoboPeak Test Cert" /t http://timestamp.digicert.com UsbDisplayIdd.cat
signtool verify /pa /v UsbDisplayIdd.cat
```

**Sign UsbTouchHidUmdf:**
```cmd
cd ..\..\UsbTouchHidUmdf\x64\Debug

inf2cat /driver:. /os:10_X64
signtool sign /v /s PrivateCertStore /n "RoboPeak Test Cert" /t http://timestamp.digicert.com UsbTouchHidUmdf.cat
signtool verify /pa /v UsbTouchHidUmdf.cat
```

### Production Signing (WHQL/EV Certificate)

**For production deployment:**

1. **Obtain EV Code Signing Certificate** from trusted CA
2. **Submit to Windows Hardware Dev Center** for attestation signing
3. **Or use Microsoft Partner Center** for WHQL certification

```cmd
REM Sign with EV certificate
signtool sign /v /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /sha1 <CertThumbprint> UsbTransportUmdf.cat
```

---

## Installing the Drivers

### Pre-Installation Checklist

- [x] Test signing enabled (`bcdedit /set testsigning on`)
- [x] System rebooted after enabling test signing
- [x] All drivers built successfully
- [x] All drivers signed with test certificate
- [x] Administrator privileges available

### Installation Methods

#### Method 1: PnPUtil (Recommended)

**Run as Administrator:**

```powershell
cd C:\path\to\rpusbdisp\drivers

# Install UsbTransportUmdf
pnputil /add-driver UsbTransportUmdf\x64\Debug\UsbTransportUmdf.inf /install

# Install UsbDisplayIdd
pnputil /add-driver UsbDisplayIdd\x64\Debug\UsbDisplayIdd.inf /install

# Install UsbTouchHidUmdf
pnputil /add-driver UsbTouchHidUmdf\x64\Debug\UsbTouchHidUmdf.inf /install

# Verify installation
pnputil /enum-drivers | findstr "RoboPeak"
```

#### Method 2: Device Manager

1. **Plug in RoboPeak USB Display**
2. Open Device Manager (`devmgmt.msc`)
3. Right-click on unknown device → **Update Driver**
4. Select **Browse my computer for drivers**
5. Navigate to `drivers\UsbTransportUmdf\x64\Debug\`
6. Click **Next**, approve test certificate warning
7. Repeat for Display and Touch drivers

#### Method 3: devcon (Command Line)

```cmd
REM Download devcon.exe from WDK samples
cd C:\Program Files (x86)\Windows Kits\10\Tools\x64

REM Install by hardware ID
devcon install C:\path\to\UsbTransportUmdf.inf "USB\VID_1FC9&PID_0094"
```

### Verify Installation

**Check Driver Status:**
```powershell
# List installed drivers
pnputil /enum-drivers

# Check device status
Get-PnpDevice | Where-Object {$_.FriendlyName -like "*RoboPeak*"}

# View in Device Manager
devmgmt.msc
```

**Expected Devices:**
- **Display adapters → RP USB Display** (UsbDisplayIdd)
- **Universal Serial Bus devices → RoboPeak USB Display Transport** (UsbTransportUmdf)
- **Human Interface Devices → RoboPeak USB Touch Screen** (UsbTouchHidUmdf)

---

## Testing and Debugging

### Enable Debug Output

#### 1. Configure WPP Tracing

**Create trace session:**
```cmd
REM Start TraceSess (from WDK)
cd C:\Program Files (x86)\Windows Kits\10\bin\x64

REM Create trace log
tracelog -start RpUsbTrace -guid #00000000-0000-0000-0000-000000000000 -f C:\Trace\rp.etl -level 5 -flags 0xFFFF

REM Stop trace
tracelog -stop RpUsbTrace

REM View trace
tracefmt C:\Trace\rp.etl -tmf C:\path\to\driver.tmf
```

#### 2. Use DebugView (SysInternals)

**Download:** https://learn.microsoft.com/en-us/sysinternals/downloads/debugview

```
1. Run DebugView as Administrator
2. Capture → Capture Kernel
3. Plug in RoboPeak device
4. Watch driver messages in real-time
```

### Live Kernel Debugging

**Setup WinDbg Connection:**

```cmd
REM On target machine (run as Admin)
bcdedit /debug on
bcdedit /dbgsettings local

REM Reboot, then attach WinDbg
windbg -k net:port=50000,key=1.2.3.4
```

**Useful WinDbg Commands:**
```
!devnode 0 1 RoboPeak        # Find device node
!wdfkd.wdfdevice <handle>    # Inspect WDF device
!wdfkd.wdfqueue <handle>     # Inspect IOCTL queue
bp UsbDisplayIdd!DriverEntry # Set breakpoint
g                             # Go/Continue
```

### Testing Display Output

**Test Pattern Application:**
```cpp
// Create 320x240 RGB565 test pattern
HDC displayDC = CreateDC(NULL, L"\\\\.\\DISPLAY2", NULL, NULL);
// Draw test pattern (red, green, blue, checkerboard)
```

### Testing Touch Input

**Touch Test Tool:**
```cmd
REM Use Windows Inbox Tool
C:\Windows\System32\MultiDigitHidTest.exe

REM Or HID test tool from WDK
C:\Program Files (x86)\Windows Kits\10\Tools\x64\dt.exe
```

---

## Troubleshooting

### Build Errors

#### Error: "Cannot find WDF headers"
**Solution:**
```
Verify WDK installation:
C:\Program Files (x86)\Windows Kits\10\Include\wdf\umdf\2.31\
```

Add to Additional Include Directories:
```
$(KIT_ROOT)\Include\wdf\umdf\$(UMDF_VERSION_MAJOR).$(UMDF_VERSION_MINOR)
```

#### Error: "KMDF not found" (for IddCx driver)
**Solution:**
```
IddCx requires KMDF context, not UMDF.
Change project settings:
Configuration Properties → Driver Settings → Driver Model: KMDF
```

#### Error: "WPP tracing failed"
**Solution:**
```
1. Verify Trace.h exists in project
2. Check WPP settings in project properties
3. Ensure tracewpp.exe is in PATH
```

### Installation Errors

#### Error 52: Windows cannot verify digital signature
**Solution:**
```powershell
# Ensure test signing enabled
bcdedit /set testsigning on
# Reboot required
shutdown /r /t 0

# Re-sign driver
signtool sign /v /s PrivateCertStore /n "RoboPeak Test Cert" /t http://timestamp.digicert.com UsbTransportUmdf.cat
```

#### Error: "Driver failed to load (Code 10)"
**Solution:**
```
1. Check Event Viewer → Windows Logs → System
2. Look for error messages from kernel
3. Verify INF file matches hardware ID
4. Check DriverEntry returns success
```

#### Device not detected
**Solution:**
```
1. Verify USB device VID:PID (should be 0x1FC9:0x0094)
2. Check Device Manager for "Unknown Device"
3. Manually update driver through Device Manager
4. Check USB cable and port
```

### Runtime Errors

#### Display not appearing
**Solution:**
```
1. Check Display Settings (Windows+P)
2. Verify monitor created: Event Viewer → IddCx logs
3. Enable verbose logging
4. Check EDID data in Edid.h (should be 320x240)
```

#### Touch not working
**Solution:**
```
1. Open Device Manager → Human Interface Devices
2. Verify "RoboPeak USB Touch Screen" present
3. Test with MultiDigitHidTest.exe
4. Check interrupt pipe is reading data
```

#### Black screen / no frames
**Solution:**
```
1. Verify USB bulk OUT pipe working
2. Check frame chunking (should be 10 chunks)
3. Monitor WPP traces for "Processing frame" messages
4. Verify present thread created successfully
```

---

## Build Automation (Optional)

### Batch Build Script

**Create `build-all.cmd`:**
```batch
@echo off
setlocal

echo ========================================
echo Building RoboPeak USB Display Drivers
echo ========================================

set BUILD_CONFIG=Debug
set BUILD_PLATFORM=x64

REM Build each driver
echo.
echo [1/3] Building UsbTransportUmdf...
msbuild UsbTransportUmdf\UsbTransportUmdf.vcxproj /p:Configuration=%BUILD_CONFIG% /p:Platform=%BUILD_PLATFORM% /v:m
if %ERRORLEVEL% NEQ 0 goto :error

echo.
echo [2/3] Building UsbDisplayIdd...
msbuild UsbDisplayIdd\UsbDisplayIdd.vcxproj /p:Configuration=%BUILD_CONFIG% /p:Platform=%BUILD_PLATFORM% /v:m
if %ERRORLEVEL% NEQ 0 goto :error

echo.
echo [3/3] Building UsbTouchHidUmdf...
msbuild UsbTouchHidUmdf\UsbTouchHidUmdf.vcxproj /p:Configuration=%BUILD_CONFIG% /p:Platform=%BUILD_PLATFORM% /v:m
if %ERRORLEVEL% NEQ 0 goto :error

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
goto :end

:error
echo.
echo ========================================
echo Build FAILED with error %ERRORLEVEL%
echo ========================================
exit /b %ERRORLEVEL%

:end
endlocal
```

### PowerShell Build Script

**Create `build-all.ps1`:**
```powershell
$ErrorActionPreference = "Stop"

Write-Host "Building RoboPeak USB Display Drivers" -ForegroundColor Cyan

$config = "Debug"
$platform = "x64"

$drivers = @(
    "UsbTransportUmdf",
    "UsbDisplayIdd",
    "UsbTouchHidUmdf"
)

foreach ($driver in $drivers) {
    Write-Host "`nBuilding $driver..." -ForegroundColor Yellow
    msbuild "$driver\$driver.vcxproj" /p:Configuration=$config /p:Platform=$platform /v:minimal

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed for $driver"
        exit 1
    }
}

Write-Host "`nAll drivers built successfully!" -ForegroundColor Green
```

---

## Quick Reference

### Essential Commands

```powershell
# Enable test signing
bcdedit /set testsigning on

# Build all drivers
msbuild rpusbdisp.sln /p:Configuration=Debug /p:Platform=x64

# Sign driver
signtool sign /v /s PrivateCertStore /n "RoboPeak Test Cert" UsbTransportUmdf.cat

# Install driver
pnputil /add-driver UsbTransportUmdf.inf /install

# View installed drivers
pnputil /enum-drivers | findstr RoboPeak

# Uninstall driver
pnputil /delete-driver oem123.inf /uninstall /force
```

### File Locations

| Component | Location |
|-----------|----------|
| WDK | `C:\Program Files (x86)\Windows Kits\10\` |
| Build Tools | `C:\Program Files\Microsoft Visual Studio\2022\` |
| Driver Output | `drivers\<DriverName>\x64\Debug\` |
| Symbols | Same as driver output (.pdb files) |
| Traces | `C:\Trace\` (or custom location) |

---

## Next Steps

After successful build and installation:

1. ✅ **Test basic functionality** - Device enumeration, display output, touch input
2. ✅ **Run WPP tracing** - Collect debug logs
3. ✅ **Performance testing** - Frame rate, touch latency
4. ✅ **HLK testing** - Prepare for certification
5. ✅ **Production signing** - Submit for attestation signing

---

## Support and Resources

### Documentation
- **Driver Development:** `docs/driver-analysis-report.md`
- **Architecture:** `docs/windows-driver-architecture.md`
- **Milestones:** `docs/milestone2-completion-report.md`, `docs/milestone3-completion-report.md`
- **Test Plan:** `docs/unit-test-plan.md`

### Microsoft Resources
- [WDF Driver Development](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/)
- [IddCx Documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview)
- [UMDF Programming Guide](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/getting-started-with-umdf-version-2)
- [Driver Signing](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/driver-signing)

### Useful Tools
- **DebugView** - Real-time debug output
- **WinDbg Preview** - Kernel debugging
- **Device Manager** - Driver installation/status
- **Event Viewer** - System logs
- **USBView** - USB device information

---

**Document Version:** 1.0
**Last Updated:** 2025-11-19
**Maintained By:** RoboPeak Driver Development Team
