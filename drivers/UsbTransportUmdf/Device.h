#pragma once

#include <wdf.h>
#include <wdfusb.h>

#include "UsbIoctl.h"
#include "UsbProtocol.h"

// Touch data ring buffer
struct TouchDataBuffer
{
    rpusb::TouchContact Contacts[rpusb::MaxTouchContacts];
    UINT8 ContactCount;
    WDFSPINLOCK Lock;
    KEVENT DataAvailable;
};

struct DeviceContext
{
    WDFUSBDEVICE UsbDevice = nullptr;
    WDFUSBINTERFACE UsbInterface = nullptr;
    WDFUSBPIPE BulkIn = nullptr;
    WDFUSBPIPE BulkOut = nullptr;
    WDFUSBPIPE InterruptIn = nullptr;
    WDFQUEUE IoctlQueue = nullptr;
    RPUSB_STATISTICS Statistics = {};
    BOOLEAN DeviceReady = FALSE;
    TouchDataBuffer TouchData = {};
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DeviceContext, GetDeviceContext)

EVT_WDF_DEVICE_PREPARE_HARDWARE UsbDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE UsbDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY UsbDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT UsbDeviceD0Exit;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL UsbDeviceIoDeviceControl;
EVT_WDF_USB_READER_COMPLETION_ROUTINE UsbInterruptCompletion;

NTSTATUS UsbDeviceCreate(_Inout_ PWDFDEVICE_INIT DeviceInit);
