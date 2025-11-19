#include "Pipeline.h"

#include <dxgi1_6.h>

#include "../UsbTransportUmdf/UsbIoctl.h"

namespace
{
    PipelineContext g_context;
}

NTSTATUS PipelineInitialize(_In_ WDFDEVICE device)
{
    UNREFERENCED_PARAMETER(device);
    // Transport binding is deferred until the USB transport driver publishes
    // a device interface.  The UMDF sample solution wires this up inside
    // DeviceArrived.  We keep a placeholder to make the code compile and to
    // document the next implementation step.
    g_context.TransportTarget = nullptr;
    return STATUS_SUCCESS;
}

void PipelineHandlePresent(_In_ IDDCX_SWAPCHAIN swapChain, _In_ const IDARG_IN_PRESENT* presentArgs)
{
    UNREFERENCED_PARAMETER(presentArgs);
    Microsoft::WRL::ComPtr<IDXGISwapChain3> dxgiSwapChain;
    swapChain->QueryInterface(dxgiSwapChain.GetAddressOf());

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    dxgiSwapChain->GetDesc1(&desc);
    UNREFERENCED_PARAMETER(desc);
    // TODO: Map and copy DXGI surfaces into RPUSB_FRAME_HEADER buffers before
    // sending IOCTL_RPUSB_PUSH_FRAME to the transport target.
}

void PipelineTeardown()
{
    if (g_context.TransportTarget != nullptr)
    {
        WdfIoTargetClose(g_context.TransportTarget);
        WdfObjectDelete(g_context.TransportTarget);
        g_context.TransportTarget = nullptr;
    }
}
