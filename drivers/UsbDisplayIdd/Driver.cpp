#include <ntddk.h>
#include <wdf.h>
#include <IddCx.h>

#include "DisplayDevice.h"
#include "Pipeline.h"

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD DisplayEvtDeviceAdd;
EVT_WDF_DRIVER_UNLOAD DisplayEvtDriverUnload;

_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, DisplayEvtDeviceAdd);
    config.DriverInitFlags |= WdfDriverInitNonPnpDriver;
    config.DriverPoolTag = 'RUSB';

    return WdfDriverCreate(driverObject, registryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

_Use_decl_annotations_
NTSTATUS DisplayEvtDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT deviceInit)
{
    UNREFERENCED_PARAMETER(driver);

    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(deviceInit, FALSE);

    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware = DisplayEvtPrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware = DisplayEvtReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &pnpCallbacks);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DisplayDeviceContext);

    WDFDEVICE device;
    NTSTATUS status = WdfDeviceCreate(&deviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    auto* context = GetDisplayContext(device);
    context->WdfDevice = device;

    status = PipelineInitialize(device);
    return status;
}

_Use_decl_annotations_
VOID DisplayEvtDriverUnload(WDFDRIVER /*driver*/)
{
    PipelineTeardown();
}
