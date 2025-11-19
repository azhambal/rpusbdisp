#pragma once

#include <IddCx.h>
#include <wdf.h>

#include <memory>

#include "Edid.h"

struct SwapChainContext
{
    IDDCX_SWAPCHAIN SwapChain = nullptr;
    PKTHREAD PresentThread = nullptr;
    KEVENT StopEvent;
    volatile BOOLEAN ShouldStop = FALSE;
};

struct DisplayDeviceContext
{
    WDFDEVICE WdfDevice = nullptr;
    IDDCX_ADAPTER Adapter = nullptr;
    IDDCX_MONITOR Monitor = nullptr;
    SwapChainContext SwapChainCtx;

    // Current active display mode
    UINT32 CurrentWidth = 800;
    UINT32 CurrentHeight = 480;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DisplayDeviceContext, GetDisplayContext)

EVT_WDF_DEVICE_PREPARE_HARDWARE DisplayEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE DisplayEvtReleaseHardware;
EVT_WDF_DEVICE_SURPRISE_REMOVAL DisplayEvtSurpriseRemoval;
EVT_WDF_DEVICE_D0_ENTRY DisplayEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT DisplayEvtD0Exit;
EVT_IDD_CX_ADAPTER_INIT_FINISHED DisplayEvtAdapterInitFinished;
EVT_IDD_CX_ADAPTER_COMMIT_MODES DisplayEvtAdapterCommitModes;
EVT_IDD_CX_ADAPTER_MONITOR_ASSIGN_SWAPCHAIN DisplayEvtAssignSwapChain;
EVT_IDD_CX_ADAPTER_MONITOR_UNASSIGN_SWAPCHAIN DisplayEvtUnassignSwapChain;
