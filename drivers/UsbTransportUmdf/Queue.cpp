#include "Queue.h"

#include <usb.h>

#include "UsbIoctl.h"
#include "UsbProtocol.h"

NTSTATUS QueueCreate(_In_ WDFDEVICE device)
{
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = UsbDeviceIoDeviceControl;

    return WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, nullptr);
}

static NTSTATUS SendVendorControl(_In_ DeviceContext* context,
                                  _In_ UINT8 request,
                                  _In_ UINT16 value,
                                  _In_reads_bytes_opt_(bufferLength) PVOID buffer,
                                  _In_ size_t bufferLength)
{
    if (!context->DeviceReady)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    WDF_MEMORY_DESCRIPTOR memoryDescriptor;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memoryDescriptor, buffer, static_cast<ULONG>(bufferLength));

    WDF_USB_CONTROL_SETUP_PACKET packet;
    WDF_USB_CONTROL_SETUP_PACKET_INIT_VENDOR(&packet,
                                             BmRequestHostToDevice,
                                             BmRequestToDevice,
                                             request,
                                             value,
                                             0);

    return WdfUsbTargetDeviceSendControlTransferSynchronously(context->UsbDevice,
                                                              nullptr,
                                                              nullptr,
                                                              &packet,
                                                              bufferLength > 0 ? &memoryDescriptor : nullptr,
                                                              nullptr);
}

VOID UsbDeviceIoDeviceControl(_In_ WDFQUEUE queue,
                              _In_ WDFREQUEST request,
                              _In_ size_t outputBufferLength,
                              _In_ size_t inputBufferLength,
                              _In_ ULONG ioControlCode)
{
    auto device = WdfIoQueueGetDevice(queue);
    auto* context = GetDeviceContext(device);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    switch (ioControlCode)
    {
    case IOCTL_RPUSB_PING:
        status = SendVendorControl(context, rpusb::kVendorRequestPing, 0, nullptr, 0);
        break;
    case IOCTL_RPUSB_GET_VERSION:
    {
        if (outputBufferLength < sizeof(RPUSB_DRIVER_VERSION))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        WDFMEMORY outputMemory;
        status = WdfRequestRetrieveOutputMemory(request, &outputMemory);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        RPUSB_DRIVER_VERSION version = {1, 0, 0};
        status = SendVendorControl(context,
                                   rpusb::kVendorRequestInit,
                                   0,
                                   &version,
                                   sizeof(version));
        if (NT_SUCCESS(status))
        {
            status = WdfMemoryCopyFromBuffer(outputMemory, 0, &version, sizeof(version));
        }
        break;
    }
    case IOCTL_RPUSB_PUSH_FRAME:
        if (inputBufferLength < sizeof(RPUSB_FRAME_HEADER))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        else
        {
            WDFMEMORY inputMemory;
            status = WdfRequestRetrieveInputMemory(request, &inputMemory);
            if (!NT_SUCCESS(status))
            {
                break;
            }

            auto* frameHeader = reinterpret_cast<RPUSB_FRAME_HEADER*>(WdfMemoryGetBuffer(inputMemory, nullptr));
            if (frameHeader->PayloadBytes == 0)
            {
                status = STATUS_INVALID_PARAMETER;
                break;
            }

            status = WdfUsbTargetPipeWriteSynchronously(context->BulkOut,
                                                         request,
                                                         nullptr,
                                                         inputMemory,
                                                         nullptr);
            if (NT_SUCCESS(status))
            {
                context->Statistics.FramesSubmitted++;
                context->Statistics.BytesTransferred += frameHeader->PayloadBytes;
            }
        }
        break;
    case IOCTL_RPUSB_GET_STATISTICS:
    {
        if (outputBufferLength < sizeof(RPUSB_STATISTICS))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        WDFMEMORY outputMemory;
        status = WdfRequestRetrieveOutputMemory(request, &outputMemory);
        if (!NT_SUCCESS(status))
        {
            break;
        }

        status = WdfMemoryCopyFromBuffer(outputMemory, 0, &context->Statistics, sizeof(RPUSB_STATISTICS));
        break;
    }
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestComplete(request, status);
}
