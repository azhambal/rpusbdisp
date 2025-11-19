#include <ntddk.h>
#include <wdf.h>

#include "Device.h"

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD UsbEvtDeviceAdd;
EVT_WDF_DRIVER_UNLOAD UsbEvtDriverUnload;

_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, UsbEvtDeviceAdd);
    config.EvtDriverUnload = UsbEvtDriverUnload;

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    return WdfDriverCreate(driverObject, registryPath, &attributes, &config, WDF_NO_HANDLE);
}

_Use_decl_annotations_
NTSTATUS UsbEvtDeviceAdd(WDFDRIVER /*driver*/, PWDFDEVICE_INIT deviceInit)
{
    return UsbDeviceCreate(deviceInit);
}

_Use_decl_annotations_
VOID UsbEvtDriverUnload(WDFDRIVER /*driver*/)
{
}
