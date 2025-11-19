#include "Device.h"

#include <ntddk.h>

NTSTATUS TouchDeviceCreate(_Inout_ PWDFDEVICE_INIT deviceInit)
{
    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(deviceInit, FALSE);

    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &pnpCallbacks);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, TouchDeviceContext);

    WDFDEVICE device;
    NTSTATUS status = WdfDeviceCreate(&deviceInit, &attributes, &device);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoInternalDeviceControl = TouchEvtInternalIoctl;

    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, nullptr);
    return status;
}

VOID TouchEvtInternalIoctl(_In_ WDFQUEUE queue,
                           _In_ WDFREQUEST request,
                           _In_ size_t /*outputBufferLength*/,
                           _In_ size_t /*inputBufferLength*/,
                           _In_ ULONG ioControlCode)
{
    UNREFERENCED_PARAMETER(queue);
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    switch (ioControlCode)
    {
    case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
    {
        PHID_DESCRIPTOR descriptor;
        size_t length;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(HID_DESCRIPTOR), reinterpret_cast<PVOID*>(&descriptor), &length);
        if (NT_SUCCESS(status))
        {
            RtlZeroMemory(descriptor, sizeof(HID_DESCRIPTOR));
            descriptor->bLength = sizeof(HID_DESCRIPTOR);
            descriptor->bDescriptorType = HID_DESCRIPTOR_TYPE_HID;
            descriptor->bcdHID = HID_REVISION;
            descriptor->bNumDescriptors = 1;
            descriptor->DescriptorList[0].bReportType = HID_REPORT_DESCRIPTOR_TYPE;
            descriptor->DescriptorList[0].wReportLength = sizeof(g_RpTouchReportDescriptor);
            WdfRequestSetInformation(request, sizeof(HID_DESCRIPTOR));
        }
        break;
    }
    case IOCTL_HID_GET_REPORT_DESCRIPTOR:
    {
        PVOID buffer = nullptr;
        size_t length = 0;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(g_RpTouchReportDescriptor), &buffer, &length);
        if (NT_SUCCESS(status))
        {
            RtlCopyMemory(buffer, g_RpTouchReportDescriptor, sizeof(g_RpTouchReportDescriptor));
            WdfRequestSetInformation(request, sizeof(g_RpTouchReportDescriptor));
        }
        break;
    }
    case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
    {
        PHID_DEVICE_ATTRIBUTES attributes = nullptr;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(HID_DEVICE_ATTRIBUTES), reinterpret_cast<PVOID*>(&attributes), nullptr);
        if (NT_SUCCESS(status))
        {
            attributes->Size = sizeof(HID_DEVICE_ATTRIBUTES);
            attributes->VendorID = 0x1FC9;
            attributes->ProductID = 0x0094;
            attributes->VersionNumber = 0x0001;
            WdfRequestSetInformation(request, sizeof(HID_DEVICE_ATTRIBUTES));
        }
        break;
    }
    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    WdfRequestComplete(request, status);
}
