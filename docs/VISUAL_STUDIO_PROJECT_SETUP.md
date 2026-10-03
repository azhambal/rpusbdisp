# Visual Studio Project Setup Guide

**Complete step-by-step guide to create Visual Studio projects for RoboPeak USB Display drivers**

---

## Table of Contents
1. [Understanding the Two Options](#understanding-the-two-options)
2. [UsbTransportUmdf Setup](#usbtransportumdf-setup)
3. [UsbDisplayIdd Setup](#usbdisplayidd-setup)
4. [UsbTouchHidUmdf Setup](#usbtouchhidumdf-setup)
5. [Troubleshooting](#troubleshooting)

---

## Understanding the Two Options

The BUILD.md guide mentions two options for creating Visual Studio projects. Here's what they mean:

### Option 1: Use WDK Templates (Recommended)

This option uses **pre-configured project templates** that come with the Windows Driver Kit (WDK). These templates automatically set up the correct project structure, compiler settings, and linker configurations for driver development.

**The workflow is:**
1. **Create a new project using a WDK template** - Visual Studio creates a skeleton driver project with example/template files
2. **Delete the template files** - Remove the auto-generated example code
3. **Add the repository's actual source files** - Import the real `.cpp`, `.h`, `.inf` files from the repository

**Why this approach?**
- The template automatically configures:
  - Correct platform toolset (WindowsUserModeDriver10.0)
  - Driver-specific build settings
  - Proper library dependencies
  - INF file handling
  - WPP tracing infrastructure
- You just need to swap out the code files

**When to use:** This is the recommended approach for most users.

### Option 2: Manual Project Creation

This option means creating a **completely empty project** and manually configuring **every single setting** yourself.

**You need to manually set:**
- Project Type: Driver (Universal Windows)
- Platform Toolset: WindowsUserModeDriver10.0
- Configuration Type: Driver
- Target Platform: Universal
- Include directories
- Library dependencies
- Linker settings
- WPP tracing configuration
- INF file processing
- And many more settings...

**Why this approach?**
- More control over every aspect
- Better understanding of project structure
- No template files to clean up
- But **much more work** and **error-prone**

**When to use:** Only if you need very specific custom configurations or want to deeply understand every project setting.

---

## UsbTransportUmdf Setup

The UsbTransportUmdf is a standard UMDF USB driver.

### Step 1-6: Create the Project

1. **Open Visual Studio 2022**
2. **File → New → Project**
3. **Search for "User Mode Driver, USB (UMDF V2)"**
4. **Name:** `UsbTransportUmdf`
5. **Location:** `C:\path\to\rpusbdisp\drivers\`
   - Note: VS will create a subfolder called `UsbTransportUmdf` inside `drivers\`
6. **Click Create**

### What Visual Studio Creates

At this point, Visual Studio creates these files:

```
drivers/UsbTransportUmdf/
├── Driver.cpp          ← Template example code
├── Driver.h            ← Template example code
├── Device.cpp          ← Template example code
├── Device.h            ← Template example code
├── Queue.cpp           ← Template example code
├── Queue.h             ← Template example code
├── Public.h            ← Template example code
├── Trace.h             ← Template tracing header
├── UsbTransportUmdf.inf    ← Template INF file
├── UsbTransportUmdf.vcxproj     ← PROJECT FILE (KEEP!)
├── UsbTransportUmdf.vcxproj.filters ← PROJECT FILE (KEEP!)
└── UsbTransportUmdf.vcxproj.user    ← PROJECT FILE (KEEP!)
```

### Step 7: Delete Template Files

**DELETE these files** (they contain example/template code):
- ✅ `Driver.cpp` (template)
- ✅ `Driver.h` (template)
- ✅ `Device.cpp` (template)
- ✅ `Device.h` (template)
- ✅ `Queue.cpp` (template)
- ✅ `Queue.h` (template)
- ✅ `Public.h` (template)
- ⚠️ `Trace.h` (you might keep this if your repository doesn't have one, or replace it)
- ⚠️ `UsbTransportUmdf.inf` (replace with repository version)

**DO NOT DELETE these files** (they are the project configuration):
- ❌ **UsbTransportUmdf.vcxproj** - This is the main project file with all build settings
- ❌ **UsbTransportUmdf.vcxproj.filters** - This organizes files in Solution Explorer
- ❌ **UsbTransportUmdf.vcxproj.user** - User-specific settings (optional, but safe to keep)

### Step 8: Add Existing Items from Repository

Now add the **real source files** from your repository:

1. Right-click on the project in Solution Explorer
2. **Add → Existing Item...**
3. Navigate to your repository's `drivers/UsbTransportUmdf/` folder (the one with the actual source code)
4. Select all the source files:
   - `Driver.cpp`
   - `Driver.h`
   - `Device.cpp`
   - `Device.h`
   - `Queue.cpp`
   - `Queue.h`
   - `UsbIoctl.h`
   - `UsbProtocol.h`
   - `Trace.h`
   - `UsbTransportUmdf.rc`
   - `UsbTransportUmdf.inf`
5. Click **Add**

### Final Structure

```
drivers/UsbTransportUmdf/
├── Driver.cpp          ← From repository
├── Driver.h            ← From repository
├── Device.cpp          ← From repository
├── Device.h            ← From repository
├── Queue.cpp           ← From repository
├── Queue.h             ← From repository
├── UsbIoctl.h          ← From repository
├── UsbProtocol.h       ← From repository
├── Trace.h             ← From repository
├── UsbTransportUmdf.rc ← From repository
├── UsbTransportUmdf.inf    ← From repository
├── UsbTransportUmdf.vcxproj     ← FROM TEMPLATE (KEEP!)
├── UsbTransportUmdf.vcxproj.filters ← FROM TEMPLATE (KEEP!)
└── UsbTransportUmdf.vcxproj.user    ← FROM TEMPLATE (KEEP!)
```

---

## UsbDisplayIdd Setup

The UsbDisplayIdd driver is special because it's an **Indirect Display Driver (IddCx)**, which is a hybrid driver type.

### Why "Kernel Mode Driver, Empty (KMDF)" Template?

There's **no direct template** for IddCx drivers in Visual Studio. IddCx drivers are unique because:
- They use the **IddCx framework** (Indirect Display Driver Class Extension)
- They need **KMDF context** but run in **user mode** (UMDF)
- So you start with a KMDF template, then configure it for IddCx

The word **"Empty"** means the template won't generate example source files - it just creates the project configuration.

### Step 1-3: Create the Project

1. **File → New → Project**
2. **Search for "Kernel Mode Driver, Empty (KMDF)"**
3. **Name:** `UsbDisplayIdd`
4. **Location:** `C:\path\to\rpusbdisp\drivers\`
5. **Click Create**

This creates:
```
drivers/UsbDisplayIdd/
├── UsbDisplayIdd.vcxproj         ← Project file (KMDF settings)
├── UsbDisplayIdd.vcxproj.filters
└── UsbDisplayIdd.vcxproj.user
```

**Note:** Since it's an "Empty" template, there are **NO source files** created - just the project files.

### Step 4: Keep KMDF Configuration

**For IddCx, you typically KEEP it as KMDF** - the IddCx framework handles the user-mode aspects internally.

You don't need to manually change from KMDF to UMDF. The project should remain configured as KMDF.

### Step 5: Add IddCx References

Now configure the IddCx-specific settings:

**Right-click project → Properties:**

#### A. Enable IddCx Framework

```
Configuration Properties → Driver Settings → General
└── Use IddCx: Yes  ← Set this to Yes
```

**Note:** If you don't see "Driver Settings", see the [Troubleshooting](#troubleshooting-missing-driver-settings) section below.

#### B. Add Include Directories

```
Configuration Properties → C/C++ → General → Additional Include Directories
```

Add:
```
$(ProjectDir);
$(ProjectDir)..\common\inc;
$(KIT_ROOT)\Include\$(SDK_VERSION)\um;
%(AdditionalIncludeDirectories)
```

#### C. Add Library Dependencies

```
Configuration Properties → Linker → Input → Additional Dependencies
```

Add these libraries:
```
OneCoreUAP.lib
$(DDK_LIB_PATH)IddCx.lib
$(DDK_LIB_PATH)wdfdrivers.lib
d3d11.lib
dxgi.lib
```

Click **Apply** and **OK**.

### Step 6: Add Source Files from Repository

**YES, you still need to copy the repository files!**

1. **Right-click on the project** in Solution Explorer
2. **Add → Existing Item...**
3. Navigate to your repository's `drivers/UsbDisplayIdd/` folder
4. Select all source files:
   - `Driver.cpp`
   - `DisplayDevice.cpp`
   - `DisplayDevice.h`
   - `Pipeline.cpp`
   - `Pipeline.h`
   - `Edid.h`
   - `Trace.h`
   - `UsbDisplayIdd.rc`
   - `UsbDisplayIdd.inf`
5. Click **Add**

### Step 7: Configure WPP Tracing

```
Configuration Properties → WPP Tracing → All Options
├── Run WPP Tracing: Yes
├── Scan Configuration Data: Trace.h
└── Function to generate trace messages: DoTraceMessage(LEVEL,FLAGS,MSG,...)
```

### Summary - Key Differences

| UsbTransportUmdf | UsbDisplayIdd |
|------------------|---------------|
| Uses "User Mode Driver, USB (UMDF V2)" template | Uses "Kernel Mode Driver, Empty (KMDF)" template |
| Template creates example source files → delete them | Template is "Empty" → no source files to delete |
| Standard UMDF driver | IddCx driver (hybrid KMDF/user-mode) |
| No special framework | Requires IddCx framework |

### Checklist for UsbDisplayIdd

- ✅ Create project from "Kernel Mode Driver, Empty (KMDF)" template
- ✅ Keep it as KMDF (IddCx requires KMDF context)
- ✅ Set "Use IddCx: Yes" in Driver Settings
- ✅ Add IddCx.lib and other libraries
- ✅ Add include directories
- ✅ Copy source files from repository
- ✅ Configure WPP tracing

---

## UsbTouchHidUmdf Setup

The UsbTouchHidUmdf is a standard UMDF HID minidriver.

### Step 1-3: Create the Project

1. **File → New → Project**
2. **Search for "User Mode Driver, Empty (UMDF V2)"**
3. **Name:** `UsbTouchHidUmdf`
4. **Location:** `C:\path\to\rpusbdisp\drivers\`
5. **Click Create**

This creates an empty UMDF project (no template source files).

### Step 4: Add HID Minidriver References

**Right-click project → Properties:**

#### A. Configure Driver Settings

```
Configuration Properties → Driver Settings → General
├── Target OS Version: Windows 11
├── Target Platform: Universal
└── Minimum UMDF Version: 2.31
```

#### B. Add Library Dependencies

```
Configuration Properties → Linker → Input → Additional Dependencies
```

Add:
```
OneCoreUAP.lib
$(DDK_LIB_PATH)wdfdrivers.lib
$(DDK_LIB_PATH)hidclass.lib
$(DDK_LIB_PATH)mshidumdf.lib
```

### Step 5: Add Source Files from Repository

1. **Right-click on the project** in Solution Explorer
2. **Add → Existing Item...**
3. Navigate to your repository's `drivers/UsbTouchHidUmdf/` folder
4. Select all source files:
   - `Driver.cpp`
   - `Device.cpp`
   - `Device.h`
   - `HidReport.h`
   - `Trace.h`
   - `UsbTouchHidUmdf.rc`
   - `UsbTouchHidUmdf.inf`
5. Click **Add**

### Step 6: Configure WPP Tracing

```
Configuration Properties → WPP Tracing → All Options
├── Run WPP Tracing: Yes
├── Scan Configuration Data: Trace.h
└── Function to generate trace messages: DoTraceMessage(LEVEL,FLAGS,MSG,...)
```

---

## Troubleshooting

### Missing "Driver Settings"

If you don't see **"Driver Settings"** in your project properties, it means the **WDK Visual Studio Extension** is either not installed or not properly loaded.

#### Why You Don't See "Driver Settings"

The **"Driver Settings"** section only appears when:
1. ✅ Windows Driver Kit (WDK) is installed
2. ✅ WDK Visual Studio Extension is enabled
3. ✅ The project is recognized as a **driver project** (not a regular C++ project)

#### What You Should See (Correct Setup)

When the WDK extension is properly installed and the project is a driver project, your **Properties** should look like this:

```
Project Properties
├── General
├── Advanced
├── Debugging
├── VC++ Directories
├── C/C++
├── Linker
├── Manifest Tool
├── XML Document Generator
├── Browse Information
├── Build Events
├── Custom Build Step
├── Driver Settings          ← THIS SECTION SHOULD EXIST
│   ├── General
│   │   ├── Target OS Version
│   │   ├── Target Platform
│   │   ├── Driver Model
│   │   ├── Use IddCx         ← For UsbDisplayIdd
│   │   └── Minimum KMDF/UMDF Version
│   ├── Driver Signing
│   ├── Driver Verifier
│   └── Deployment
└── WPP Tracing              ← THIS SECTION SHOULD ALSO EXIST
    ├── All Options
    └── ...
```

#### Solution 1: Verify WDK Extension is Installed

1. **Open Visual Studio 2022**
2. Go to **Extensions → Manage Extensions**
3. Click on **Installed** tab
4. Search for **"Windows Driver Kit"** or **"WDK"**
5. You should see: **"WDK for Windows 11"** or similar

**If you DON'T see it:**
- The WDK extension is not installed
- You need to install it (see Solution 2 below)

**If you DO see it but it's disabled:**
- Click **Enable** and restart Visual Studio

#### Solution 2: Install WDK Visual Studio Extension

If the extension is missing:

1. **Close Visual Studio**
2. **Run the WDK installer again** (download from [Microsoft WDK page](https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk))
3. During installation, make sure to check:
   - ✅ **"Install Windows Driver Kit"**
   - ✅ **"Install Visual Studio Extension"** ← This is critical!
4. Complete the installation
5. **Restart your computer**
6. **Open Visual Studio** and verify the extension is loaded

#### Solution 3: Verify the Project is a Driver Project

When you created the project, did you use a **driver template**?
- ✅ "Kernel Mode Driver, Empty (KMDF)"
- ✅ "User Mode Driver, USB (UMDF V2)"
- ✅ "User Mode Driver, Empty (UMDF V2)"

If you accidentally created a **regular C++ project**, Visual Studio won't show driver settings.

**To check:**
1. Right-click on project → **Unload Project**
2. Right-click again → **Edit [ProjectName].vcxproj**
3. Look near the top for:

```xml
<PropertyGroup>
  <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
  <ConfigurationType>Driver</ConfigurationType>
  <DriverType>KMDF</DriverType>
</PropertyGroup>
```

Or for UMDF:

```xml
<PropertyGroup>
  <PlatformToolset>WindowsUserModeDriver10.0</PlatformToolset>
  <ConfigurationType>Driver</ConfigurationType>
  <DriverType>UMDF</DriverType>
</PropertyGroup>
```

**If you see this** → It's a driver project, but the extension isn't loaded
**If you DON'T see this** → You created a regular C++ project by mistake

#### Solution 4: Recreate the Project

If you've now installed the WDK extension but your project still doesn't show "Driver Settings":

1. **Create a NEW project** using the appropriate driver template
2. Verify you see "Driver Settings" this time
3. Follow the setup instructions to add your source files

#### Solution 5: Manually Convert to Driver Project

If you want to convert your existing project to a driver project:

1. Right-click on project → **Unload Project**
2. Right-click again → **Edit [ProjectName].vcxproj**
3. Find the `<PropertyGroup>` section that looks like this:

```xml
<PropertyGroup Label="Configuration">
  <ConfigurationType>Application</ConfigurationType>
  <PlatformToolset>v143</PlatformToolset>
</PropertyGroup>
```

4. **For KMDF driver, replace with:**

```xml
<PropertyGroup Label="Configuration">
  <ConfigurationType>Driver</ConfigurationType>
  <DriverType>KMDF</DriverType>
  <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
  <TargetVersion>Windows10</TargetVersion>
  <UseDebugLibraries Condition="'$(Configuration)'=='Debug'">true</UseDebugLibraries>
  <UseDebugLibraries Condition="'$(Configuration)'=='Release'">false</UseDebugLibraries>
</PropertyGroup>
```

5. **For UMDF driver, replace with:**

```xml
<PropertyGroup Label="Configuration">
  <ConfigurationType>Driver</ConfigurationType>
  <DriverType>UMDF</DriverType>
  <PlatformToolset>WindowsUserModeDriver10.0</PlatformToolset>
  <TargetVersion>Windows10</TargetVersion>
  <UseDebugLibraries Condition="'$(Configuration)'=='Debug'">true</UseDebugLibraries>
  <UseDebugLibraries Condition="'$(Configuration)'=='Release'">false</UseDebugLibraries>
</PropertyGroup>
```

6. Save and close
7. Right-click → **Reload Project**
8. Now check Properties - you should see "Driver Settings"

#### Quick Diagnostic Commands

Run these in **PowerShell** to verify WDK installation:

```powershell
# Check if WDK is installed
Test-Path "C:\Program Files (x86)\Windows Kits\10\Include\wdf"

# Check WDK version
Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include\wdf\umdf"

# Check if Visual Studio can find WDK
dir "C:\Program Files (x86)\Windows Kits\10\Vsix\VS2022" -ErrorAction SilentlyContinue
```

#### Most Common Issues

1. ❌ **WDK Visual Studio Extension not installed**
   - Solution: Run WDK installer again, select "Install Visual Studio Extension"

2. ❌ **Project wasn't created from driver template**
   - Solution: Recreate project using appropriate driver template

3. ❌ **Visual Studio version incompatibility**
   - Solution: Ensure you're using Visual Studio 2022 (version 17.0 or later)

---

## Quick Reference

### Project Creation Summary

| Driver | Template to Use | Special Steps |
|--------|----------------|---------------|
| **UsbTransportUmdf** | "User Mode Driver, USB (UMDF V2)" | Delete template files, add repository files |
| **UsbDisplayIdd** | "Kernel Mode Driver, Empty (KMDF)" | Enable IddCx, add IddCx.lib, d3d11.lib, dxgi.lib |
| **UsbTouchHidUmdf** | "User Mode Driver, Empty (UMDF V2)" | Add HID libraries: hidclass.lib, mshidumdf.lib |

### Files to Keep vs Delete

| File Type | Action |
|-----------|--------|
| `.vcxproj` | ✅ KEEP - Project configuration |
| `.vcxproj.filters` | ✅ KEEP - File organization |
| `.vcxproj.user` | ✅ KEEP - User settings |
| Template `.cpp`/`.h` | ❌ DELETE - Example code |
| Template `.inf` | ⚠️ REPLACE - Use repository version |

### Essential Configuration

For ALL drivers, you must configure:

1. **Include Directories:**
   ```
   $(ProjectDir);
   $(ProjectDir)..\common\inc;
   %(AdditionalIncludeDirectories)
   ```

2. **WPP Tracing:**
   ```
   Run WPP Tracing: Yes
   Scan Configuration Data: Trace.h
   ```

3. **Platform:**
   ```
   Configuration: Debug x64
   Platform: x64
   Target: Windows 11
   ```

---

## Next Steps

After creating all three projects:

1. ✅ Build each driver individually to verify configuration
2. ✅ Fix any include path or linker errors
3. ✅ Proceed to signing and installation (see BUILD.md)

---

**Document Version:** 1.0
**Last Updated:** 2025-11-20
**Related Documents:** BUILD.md, windows-driver-architecture.md
