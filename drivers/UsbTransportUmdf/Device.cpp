#include "Device.h"

#include <initguid.h>
#include <usb.h>

#include "Queue.h"

namespace
{
    constexpr USHORT kVendorId = 0x1FC9;
    constexpr USHORT kProductId = 0x0094;
}

NTSTATUS UsbDeviceCreate(_Inout_ PWDFDEVICE_INIT deviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware = UsbDevicePrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware = UsbDeviceReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &pnpCallbacks);

    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(deviceInit, FALSE);

    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_FILEOBJECT_CONFIG_INIT(&fileConfig, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK);
    WdfDeviceInitSetFileObjectConfig(deviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DeviceContext);

    WDFDEVICE device;
    NTSTATUS status = WdfDeviceCreate(&deviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_RPUSB_TRANSPORT, nullptr);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    DeviceContext* context = GetDeviceContext(device);
    context->Statistics = {};

    // Initialize touch data buffer
    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = device;
    status = WdfSpinLockCreate(&lockAttributes, &context->TouchData.Lock);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    KeInitializeEvent(&context->TouchData.DataAvailable, SynchronizationEvent, FALSE);
    context->TouchData.ContactCount = 0;
    RtlZeroMemory(context->TouchData.Contacts, sizeof(context->TouchData.Contacts));

    status = QueueCreate(device);
    return status;
}

NTSTATUS UsbDevicePrepareHardware(_In_ WDFDEVICE device,
                                  _In_ WDFCMRESLIST /*resourcesRaw*/,
                                  _In_ WDFCMRESLIST /*resourcesTranslated*/)
{
    auto* context = GetDeviceContext(device);

    WDF_USB_DEVICE_CREATE_CONFIG config;
    WDF_USB_DEVICE_CREATE_CONFIG_INIT(&config, USBD_CLIENT_CONTRACT_VERSION_602);

    NTSTATUS status = WdfUsbTargetDeviceCreateWithParameters(device, &config, WDF_NO_OBJECT_ATTRIBUTES, &context->UsbDevice);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    USB_DEVICE_DESCRIPTOR descriptor;
    status = WdfUsbTargetDeviceRetrieveDeviceDescriptor(context->UsbDevice, &descriptor);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    if (descriptor.idVendor != kVendorId || descriptor.idProduct != kProductId)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    status = WdfUsbTargetDeviceSelectConfig(context->UsbDevice, WDF_NO_OBJECT_ATTRIBUTES, nullptr);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    context->UsbInterface = WdfUsbTargetDeviceGetInterface(context->UsbDevice, 0);
    if (context->UsbInterface == nullptr)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    ULONG pipeCount = WdfUsbInterfaceGetNumConfiguredPipes(context->UsbInterface);
    for (ULONG pipeIndex = 0; pipeIndex < pipeCount; ++pipeIndex)
    {
        WDFUSBPIPE pipe = WdfUsbInterfaceGetConfiguredPipe(context->UsbInterface, pipeIndex, nullptr);
        WDF_USB_PIPE_INFORMATION pipeInfo;
        WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
        WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);

        if (WdfUsbPipeTypeBulk == pipeInfo.PipeType && WdfUsbTargetPipeIsInEndpoint(pipe))
        {
            context->BulkIn = pipe;
        }
        else if (WdfUsbPipeTypeBulk == pipeInfo.PipeType && WdfUsbTargetPipeIsOutEndpoint(pipe))
        {
            context->BulkOut = pipe;
        }
        else if (WdfUsbPipeTypeInterrupt == pipeInfo.PipeType && WdfUsbTargetPipeIsInEndpoint(pipe))
        {
            context->InterruptIn = pipe;
        }
    }

    if (context->BulkIn == nullptr || context->BulkOut == nullptr)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (context->InterruptIn != nullptr)
    {
        WDF_USB_CONTINUOUS_READER_CONFIG readerConfig;
        WDF_USB_CONTINUOUS_READER_CONFIG_INIT(&readerConfig, UsbInterruptCompletion, rpusb::DefaultInterruptPacketBytes);
        readerConfig.EvtUsbTargetPipeReadersFailed = nullptr;
        status = WdfUsbTargetPipeConfigContinuousReader(context->InterruptIn, &readerConfig);
        if (!NT_SUCCESS(status))
        {
            return status;
        }
    }

    context->DeviceReady = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS UsbDeviceReleaseHardware(_In_ WDFDEVICE device,
                                  _In_ WDFCMRESLIST /*resourcesTranslated*/)
{
    auto* context = GetDeviceContext(device);
    UNREFERENCED_PARAMETER(context);
    return STATUS_SUCCESS;
}

VOID UsbInterruptCompletion(_In_ WDFUSBPIPE pipe,
                            _In_ WDFMEMORY buffer,
                            _In_ size_t numBytesTransferred,
                            _In_ WDFCONTEXT context)
{
    UNREFERENCED_PARAMETER(context);

    if (numBytesTransferred < sizeof(rpusb::InterruptPacket))
    {
        // Packet too small, ignore
        return;
    }

    WDFDEVICE device = WdfIoTargetGetDevice(WdfUsbTargetPipeGetIoTarget(pipe));
    auto* deviceContext = GetDeviceContext(device);

    auto* packet = static_cast<rpusb::InterruptPacket*>(WdfMemoryGetBuffer(buffer, nullptr));
    if (packet == nullptr)
    {
        return;
    }

    switch (packet->PacketType)
    {
        case rpusb::InterruptPacketType::Touch:
        {
            // Parse and store touch data
            WdfSpinLockAcquire(deviceContext->TouchData.Lock);

            // Simple strategy: store the touch contact based on ContactId
            UINT8 contactId = packet->Data.Touch.ContactId;
            if (contactId < rpusb::MaxTouchContacts)
            {
                deviceContext->TouchData.Contacts[contactId] = packet->Data.Touch;

                // Update contact count
                // Count how many contacts have TipSwitch set
                UINT8 activeCount = 0;
                for (UINT32 i = 0; i < rpusb::MaxTouchContacts; ++i)
                {
                    if (deviceContext->TouchData.Contacts[i].TipSwitch)
                    {
                        activeCount++;
                    }
                }
                deviceContext->TouchData.ContactCount = activeCount;
            }

            WdfSpinLockRelease(deviceContext->TouchData.Lock);

            // Signal that new touch data is available
            KeSetEvent(&deviceContext->TouchData.DataAvailable, IO_NO_INCREMENT, FALSE);
            break;
        }

        case rpusb::InterruptPacketType::Status:
        {
            // Handle status packet
            // Update statistics or handle errors
            if (packet->Data.Status.ErrorCode != 0)
            {
                // Log error or update statistics
                // For now, just ignore
            }
            break;
        }

        default:
            // Unknown packet type, ignore
            break;
    }
}
