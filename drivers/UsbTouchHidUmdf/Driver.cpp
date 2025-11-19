#include <ntddk.h>
#include <wdf.h>

#include "Device.h"

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD TouchEvtDeviceAdd;
EVT_WDF_DRIVER_UNLOAD TouchEvtDriverUnload;

_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, TouchEvtDeviceAdd);

    return WdfDriverCreate(driverObject, registryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

_Use_decl_annotations_
NTSTATUS TouchEvtDeviceAdd(WDFDRIVER /*driver*/, PWDFDEVICE_INIT deviceInit)
{
    return TouchDeviceCreate(deviceInit);
}

_Use_decl_annotations_
VOID TouchEvtDriverUnload(WDFDRIVER /*driver*/)
{
}
