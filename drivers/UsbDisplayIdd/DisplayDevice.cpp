#include "DisplayDevice.h"
#include "Pipeline.h"

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

    VOID PresentProcessingThread(_In_ PVOID context)
    {
        auto* swapChainCtx = static_cast<SwapChainContext*>(context);

        while (!swapChainCtx->ShouldStop)
        {
            // Wait for a short interval or until stop event is signaled
            LARGE_INTEGER timeout;
            timeout.QuadPart = -100000LL; // 10ms in 100-nanosecond units (negative for relative)

            NTSTATUS waitStatus = KeWaitForSingleObject(&swapChainCtx->StopEvent,
                                                        Executive,
                                                        KernelMode,
                                                        FALSE,
                                                        &timeout);
            if (waitStatus != STATUS_TIMEOUT)
            {
                // Stop event was signaled
                break;
            }

            // Try to acquire and release a buffer from the swap chain
            IDARG_OUT_RELEASEANDACQUIREBUFFER buffer = {};
            NTSTATUS status = IddCxSwapChainReleaseAndAcquireBuffer(swapChainCtx->SwapChain, &buffer);

            if (NT_SUCCESS(status))
            {
                // Process the present - PipelineHandlePresent will call IddCxSwapChainFinishedPresent
                if (buffer.pSurfaceAvailable != nullptr)
                {
                    IDARG_IN_PRESENT presentArgs = {};
                    presentArgs.SurfaceAvailable = *buffer.pSurfaceAvailable;
                    PipelineHandlePresent(swapChainCtx->SwapChain, &presentArgs);
                }
            }
            else if (status == STATUS_TIMEOUT || status == STATUS_NO_MORE_ENTRIES)
            {
                // No buffer available, continue loop
                continue;
            }
            else
            {
                // Error occurred, stop processing
                break;
            }
        }

        PsTerminateSystemThread(STATUS_SUCCESS);
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
    auto* context = reinterpret_cast<DisplayDeviceContext*>(args->pContext);

    // Tell IddCx we will process frames in software (no GPU acceleration)
    IDARG_IN_SWAPCHAINSETDEVICE setDevice = {};
    setDevice.pSwapChain = args->hSwapChain;
    setDevice.pDevice = nullptr; // Software processing

    NTSTATUS status = IddCxSwapChainSetDevice(&setDevice);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    // Initialize swap-chain context
    context->SwapChainCtx.SwapChain = args->hSwapChain;
    context->SwapChainCtx.ShouldStop = FALSE;
    KeInitializeEvent(&context->SwapChainCtx.StopEvent, NotificationEvent, FALSE);

    // Create the present processing thread
    HANDLE threadHandle = nullptr;
    status = PsCreateSystemThread(&threadHandle,
                                  THREAD_ALL_ACCESS,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  PresentProcessingThread,
                                  &context->SwapChainCtx);

    if (NT_SUCCESS(status))
    {
        // Convert thread handle to thread object
        status = ObReferenceObjectByHandle(threadHandle,
                                          THREAD_ALL_ACCESS,
                                          *PsThreadType,
                                          KernelMode,
                                          reinterpret_cast<PVOID*>(&context->SwapChainCtx.PresentThread),
                                          nullptr);
        ZwClose(threadHandle);

        if (!NT_SUCCESS(status))
        {
            // Failed to reference thread, signal stop and wait for thread to exit
            context->SwapChainCtx.ShouldStop = TRUE;
            KeSetEvent(&context->SwapChainCtx.StopEvent, IO_NO_INCREMENT, FALSE);
        }
    }

    return status;
}

_Use_decl_annotations_
NTSTATUS DisplayEvtUnassignSwapChain(IDDCX_MONITOR monitor, const IDARG_IN_UNASSIGN_SWAPCHAIN* args)
{
    UNREFERENCED_PARAMETER(monitor);

    auto* context = reinterpret_cast<DisplayDeviceContext*>(args->pContext);

    // Signal the present thread to stop
    if (context->SwapChainCtx.PresentThread != nullptr)
    {
        context->SwapChainCtx.ShouldStop = TRUE;
        KeSetEvent(&context->SwapChainCtx.StopEvent, IO_NO_INCREMENT, FALSE);

        // Wait for the thread to terminate
        KeWaitForSingleObject(context->SwapChainCtx.PresentThread,
                             Executive,
                             KernelMode,
                             FALSE,
                             nullptr);

        // Release the thread object reference
        ObDereferenceObject(context->SwapChainCtx.PresentThread);
        context->SwapChainCtx.PresentThread = nullptr;
    }

    // Clear swap-chain context
    context->SwapChainCtx.SwapChain = nullptr;

    return STATUS_SUCCESS;
}
