#include "DisplayDevice.h"

#include <ntddk.h>

namespace
{
    IDARG_IN_ADAPTER_CREATE CreateAdapterArgs(WDFDEVICE device, DisplayDeviceContext* context)
    {
        IDARG_IN_ADAPTER_CREATE args = {};
        args.pWdfDevice = device;
        args.DriverConfig.ClientId = L"RoboPeakUsbDisplay";
        args.DriverConfig.EvtIddCxAdapterInitFinished = DisplayEvtAdapterInitFinished;
        args.DriverConfig.EvtIddCxAdapterCommitModes = DisplayEvtAdapterCommitModes;
        args.DriverConfig.EvtIddCxAdapterMonitorAssignSwapChain = DisplayEvtAssignSwapChain;
        args.DriverConfig.EvtIddCxAdapterMonitorUnassignSwapChain = DisplayEvtUnassignSwapChain;
        args.DriverConfig.Flags.Value = 0;
        args.pAdapterContext = context;
        return args;
    }
}

_Use_decl_annotations_
NTSTATUS DisplayEvtPrepareHardware(WDFDEVICE device,
                                   WDFCMRESLIST /*resourcesRaw*/,
                                   WDFCMRESLIST /*resourcesTranslated*/)
{
    auto* context = GetDisplayContext(device);

    IDARG_IN_ADAPTER_CREATE create = CreateAdapterArgs(device, context);
    IDARG_OUT_ADAPTER_CREATE createOut = {};
    NTSTATUS status = IddCxAdapterInitAsync(&create, sizeof(create), &createOut);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    context->Adapter = createOut.AdapterObject;
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS DisplayEvtReleaseHardware(WDFDEVICE device,
                                   WDFCMRESLIST /*resourcesTranslated*/)
{
    auto* context = GetDisplayContext(device);
    if (context->Monitor != nullptr)
    {
        IddCxMonitorUnregister(context->Monitor);
        context->Monitor = nullptr;
    }
    if (context->Adapter != nullptr)
    {
        IddCxAdapterClose(context->Adapter);
        context->Adapter = nullptr;
    }
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS DisplayEvtAdapterInitFinished(IDDCX_ADAPTER adapter, const IDARG_IN_ADAPTER_INIT_FINISHED* args)
{
    auto* context = reinterpret_cast<DisplayDeviceContext*>(args->pContext);

    IDARG_OUT_MONITOR_CREATE monitorCreate = {};
    IDARG_IN_MONITORCREATE monitorCreateArgs = {};
    monitorCreateArgs.AdapterObject = adapter;
    monitorCreateArgs.MonitorInfo.Size = sizeof(IDDCX_MONITOR_INFO);
    monitorCreateArgs.MonitorInfo.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_USB;
    monitorCreateArgs.MonitorInfo.ConnectorType = DISPLAYCONFIG_CONNECTOR_TYPE_USB;
    monitorCreateArgs.MonitorInfo.MonitorDescription.pEdid = rpusb::idd::kEdid800x480;
    monitorCreateArgs.MonitorInfo.MonitorDescription.EdidLength = sizeof(rpusb::idd::kEdid800x480);

    NTSTATUS status = IddCxMonitorCreate(&monitorCreateArgs, &monitorCreate);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    IDDCX_MONITOR_MODE mode = {};
    mode.Size = sizeof(IDDCX_MONITOR_MODE);
    mode.VideoSignalInfo.activeSize.cx = 800;
    mode.VideoSignalInfo.activeSize.cy = 480;
    mode.VideoSignalInfo.vSyncFreq.Numerator = 60;
    mode.VideoSignalInfo.vSyncFreq.Denominator = 1;
    mode.BitsPerPixel = 16;
    mode.ColorBasis = IDDCX_COLOR_BASIS_SRGB;
    mode.PixelFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

    IDARG_IN_MONITORARRIVAL arrival = {};
    arrival.AdapterObject = adapter;
    arrival.MonitorObject = monitorCreate.MonitorObject;
    arrival.MonitorModes = &mode;
    arrival.MonitorModeCount = 1;
    arrival.DefaultMonitorModeIndex = 0;

    status = IddCxMonitorArrival(&arrival);
    if (NT_SUCCESS(status))
    {
        context->Monitor = monitorCreate.MonitorObject;
    }
    return status;
}

_Use_decl_annotations_
NTSTATUS DisplayEvtAdapterCommitModes(IDDCX_ADAPTER adapter, const IDARG_IN_COMMIT_MODES* args)
{
    UNREFERENCED_PARAMETER(adapter);
    UNREFERENCED_PARAMETER(args);
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS DisplayEvtAssignSwapChain(IDDCX_MONITOR monitor, const IDARG_IN_ASSIGN_SWAPCHAIN* args)
{
    UNREFERENCED_PARAMETER(monitor);
    UNREFERENCED_PARAMETER(args);
    // TODO: hook into Pipeline.cpp and start consuming frames.
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS DisplayEvtUnassignSwapChain(IDDCX_MONITOR monitor, const IDARG_IN_UNASSIGN_SWAPCHAIN* args)
{
    UNREFERENCED_PARAMETER(monitor);
    UNREFERENCED_PARAMETER(args);
    return STATUS_SUCCESS;
}
