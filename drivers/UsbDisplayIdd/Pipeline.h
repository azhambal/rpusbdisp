#pragma once

#include <IddCx.h>
#include <wrl/client.h>
#include <wdf.h>

#include <functional>

struct PipelineContext
{
    WDFIOTARGET TransportTarget = nullptr;
};

NTSTATUS PipelineInitialize(_In_ WDFDEVICE device);
void PipelineHandlePresent(_In_ IDDCX_SWAPCHAIN swapChain, _In_ const IDARG_IN_PRESENT* presentArgs);
void PipelineTeardown();
