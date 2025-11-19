#include "Pipeline.h"

#include <dxgi1_6.h>
#include <ntddk.h>

#include "../UsbTransportUmdf/UsbIoctl.h"
#include "../UsbTransportUmdf/UsbProtocol.h"

namespace
{
    constexpr ULONG kFramePoolTag = 'frpR';

    PipelineContext g_context;

    struct PresentCompletion
    {
        explicit PresentCompletion(IDDCX_SWAPCHAIN chain) : SwapChain(chain) {}
        ~PresentCompletion()
        {
            if (SwapChain != nullptr)
            {
                IddCxSwapChainFinishedPresent(SwapChain);
            }
        }

        IDDCX_SWAPCHAIN SwapChain;
    };

    void CloseTransportTarget()
    {
        if (g_context.TransportTarget != nullptr)
        {
            WdfIoTargetClose(g_context.TransportTarget);
            WdfObjectDelete(g_context.TransportTarget);
            g_context.TransportTarget = nullptr;
        }
    }

    NTSTATUS EnsureTransportTarget()
    {
        if (g_context.TransportTarget != nullptr)
        {
            return STATUS_SUCCESS;
        }

        if (g_context.ParentDevice == nullptr)
        {
            return STATUS_INVALID_DEVICE_STATE;
        }

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

        status = WdfIoTargetCreate(g_context.ParentDevice, WDF_NO_OBJECT_ATTRIBUTES, &g_context.TransportTarget);
        if (NT_SUCCESS(status))
        {
            WDF_IO_TARGET_OPEN_PARAMS openParams;
            WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&openParams, &targetName, GENERIC_READ | GENERIC_WRITE);
            openParams.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
            openParams.CreateDisposition = FILE_OPEN;

            status = WdfIoTargetOpen(g_context.TransportTarget, &openParams);
            if (!NT_SUCCESS(status))
            {
                WdfObjectDelete(g_context.TransportTarget);
                g_context.TransportTarget = nullptr;
            }
        }

        ExFreePool(symbolicLinkList);
        return status;
    }

    void ConvertBgraToRgb565(const BYTE* srcRow, UINT32 width, UINT16* dstRow)
    {
        for (UINT32 x = 0; x < width; ++x)
        {
            const BYTE* pixel = srcRow + (x * 4);
            BYTE b = pixel[0];
            BYTE g = pixel[1];
            BYTE r = pixel[2];
            dstRow[x] = static_cast<UINT16>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
}

NTSTATUS PipelineInitialize(_In_ WDFDEVICE device)
{
    g_context.ParentDevice = device;

    NTSTATUS status = EnsureTransportTarget();
    if (!NT_SUCCESS(status))
    {
        // The display stack may load before the USB transport enumerates.  Defer
        // binding until the first present if we cannot locate the interface
        // yet to keep the adapter online for hot-plug scenarios.
        status = STATUS_SUCCESS;
    }
    return status;
}

void PipelineHandlePresent(_In_ IDDCX_SWAPCHAIN swapChain, _In_ const IDARG_IN_PRESENT* presentArgs)
{
    UNREFERENCED_PARAMETER(presentArgs);

    PresentCompletion completion(swapChain);

    if (!NT_SUCCESS(EnsureTransportTarget()))
    {
        return;
    }

    Microsoft::WRL::ComPtr<IDXGISwapChain3> dxgiSwapChain;
    if (FAILED(swapChain->QueryInterface(dxgiSwapChain.GetAddressOf())))
    {
        return;
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    if (FAILED(dxgiSwapChain->GetDesc1(&desc)))
    {
        return;
    }

    const UINT32 width = desc.Width;
    const UINT32 height = desc.Height;
    if (width == 0 || height == 0 || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        return;
    }

    Microsoft::WRL::ComPtr<IDXGISurface1> surface;
    if (FAILED(dxgiSwapChain->GetBuffer(0, IID_PPV_ARGS(&surface))))
    {
        return;
    }

    DXGI_MAPPED_RECT mapped = {};
    if (FAILED(surface->Map(&mapped, DXGI_MAP_READ)))
    {
        return;
    }

    const UINT32 payloadBytes = width * height * sizeof(UINT16);
    const size_t totalBytes = sizeof(RPUSB_FRAME_HEADER) + payloadBytes;
    auto* frameBuffer = static_cast<BYTE*>(ExAllocatePoolWithTag(NonPagedPoolNx, totalBytes, kFramePoolTag));
    if (frameBuffer == nullptr)
    {
        surface->Unmap();
        return;
    }

    auto* frameHeader = reinterpret_cast<RPUSB_FRAME_HEADER*>(frameBuffer);
    frameHeader->Width = width;
    frameHeader->Height = height;
    frameHeader->PixelFormat = static_cast<UINT32>(rpusb::PixelFormat::Rgb565);
    frameHeader->PayloadBytes = payloadBytes;

    auto* pixelData = reinterpret_cast<UINT16*>(frameHeader + 1);
    for (UINT32 row = 0; row < height; ++row)
    {
        const BYTE* srcRow = mapped.pBits + (row * mapped.Pitch);
        UINT16* dstRow = pixelData + (row * width);
        ConvertBgraToRgb565(srcRow, width, dstRow);
    }

    surface->Unmap();

    WDF_MEMORY_DESCRIPTOR inputDesc;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputDesc, frameBuffer, static_cast<ULONG>(totalBytes));

    NTSTATUS status = WdfIoTargetSendIoctlSynchronously(g_context.TransportTarget,
                                                        nullptr,
                                                        IOCTL_RPUSB_PUSH_FRAME,
                                                        &inputDesc,
                                                        nullptr,
                                                        nullptr);
    if (!NT_SUCCESS(status))
    {
        CloseTransportTarget();
    }

    ExFreePool(frameBuffer);
}

void PipelineTeardown()
{
    CloseTransportTarget();
    g_context.ParentDevice = nullptr;
}
