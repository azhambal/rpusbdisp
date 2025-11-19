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

    WDFDEVICE device;
    NTSTATUS status = WdfDeviceCreate(&deviceInit, WDF_NO_OBJECT_ATTRIBUTES, &device);
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
        WDF_USB_CONTINUOUS_READER_CONFIG_INIT(&readerConfig, UsbInterruptCompletion, rpusb::DefaultBulkPacketBytes);
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
    UNREFERENCED_PARAMETER(pipe);
    UNREFERENCED_PARAMETER(buffer);
    UNREFERENCED_PARAMETER(numBytesTransferred);
    UNREFERENCED_PARAMETER(context);
    // TODO: translate vendor notifications into events for registered listeners.
}
