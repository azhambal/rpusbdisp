#include "Device.h"

#include <ntddk.h>
#include <initguid.h>

// Include USB transport driver IOCTLs
#include "../UsbTransportUmdf/UsbIoctl.h"

namespace
{
    NTSTATUS EnsureUsbTransportTarget(_In_ WDFDEVICE device)
    {
        auto* context = GetTouchContext(device);

        if (context->UsbTransportTarget != nullptr)
        {
            return STATUS_SUCCESS;
        }

        // Find the USB transport driver device interface
        PWSTR symbolicLinkList = nullptr;
        NTSTATUS status = IoGetDeviceInterfaces(&GUID_DEVINTERFACE_RPUSB_TRANSPORT,
                                                nullptr,
                                                DEVICE_INTERFACE_ACTIVE,
                                                &symbolicLinkList);
        if (!NT_SUCCESS(status))
        {
            return status;
        }

        if (symbolicLinkList[0] == UNICODE_NULL)
        {
            ExFreePool(symbolicLinkList);
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        UNICODE_STRING targetName;
        RtlInitUnicodeString(&targetName, symbolicLinkList);

        status = WdfIoTargetCreate(device, WDF_NO_OBJECT_ATTRIBUTES, &context->UsbTransportTarget);
        if (NT_SUCCESS(status))
        {
            WDF_IO_TARGET_OPEN_PARAMS openParams;
            WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&openParams, &targetName, GENERIC_READ | GENERIC_WRITE);
            openParams.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
            openParams.CreateDisposition = FILE_OPEN;

            status = WdfIoTargetOpen(context->UsbTransportTarget, &openParams);
            if (!NT_SUCCESS(status))
            {
                WdfObjectDelete(context->UsbTransportTarget);
                context->UsbTransportTarget = nullptr;
            }
        }

        ExFreePool(symbolicLinkList);
        return status;
    }
}

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
    case IOCTL_HID_READ_REPORT:
    {
        WDFDEVICE device = WdfIoQueueGetDevice(queue);
        auto* context = GetTouchContext(device);

        // Ensure we have a connection to the USB transport driver
        status = EnsureUsbTransportTarget(device);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        // Get touch data from USB transport driver
        RPUSB_TOUCH_DATA touchData = {};
        WDF_MEMORY_DESCRIPTOR outputDesc;
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outputDesc, &touchData, sizeof(touchData));

        status = WdfIoTargetSendIoctlSynchronously(context->UsbTransportTarget,
                                                   nullptr,
                                                   IOCTL_RPUSB_GET_TOUCH_DATA,
                                                   nullptr,
                                                   &outputDesc,
                                                   nullptr);

        if (!NT_SUCCESS(status) || touchData.ContactCount == 0)
        {
            // No touch data available, return an empty report
            HID_TOUCH_INPUT_REPORT* report = nullptr;
            size_t length = 0;
            status = WdfRequestRetrieveOutputBuffer(request, sizeof(HID_TOUCH_INPUT_REPORT), reinterpret_cast<PVOID*>(&report), &length);
            if (NT_SUCCESS(status))
            {
                RtlZeroMemory(report, sizeof(HID_TOUCH_INPUT_REPORT));
                report->ReportId = 1;
                report->ContactCount = 0;
                WdfRequestSetInformation(request, sizeof(HID_TOUCH_INPUT_REPORT));
                status = STATUS_SUCCESS;
            }
            break;
        }

        // Generate HID input report from touch data
        // For simplicity, we only report the first contact
        HID_TOUCH_INPUT_REPORT* report = nullptr;
        size_t length = 0;
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(HID_TOUCH_INPUT_REPORT), reinterpret_cast<PVOID*>(&report), &length);
        if (NT_SUCCESS(status))
        {
            RtlZeroMemory(report, sizeof(HID_TOUCH_INPUT_REPORT));
            report->ReportId = 1;

            // Find the first active contact
            for (UINT32 i = 0; i < 2; ++i)
            {
                if (touchData.Contacts[i].TipSwitch || touchData.Contacts[i].InRange)
                {
                    report->TipSwitch = touchData.Contacts[i].TipSwitch;
                    report->InRange = touchData.Contacts[i].InRange;
                    report->ContactId = touchData.Contacts[i].ContactId;
                    report->X = touchData.Contacts[i].X;
                    report->Y = touchData.Contacts[i].Y;
                    break;
                }
            }

            report->ContactCount = touchData.ContactCount;
            WdfRequestSetInformation(request, sizeof(HID_TOUCH_INPUT_REPORT));
        }
        break;
    }
    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    WdfRequestComplete(request, status);
}
