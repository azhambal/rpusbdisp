#pragma once

#include <wdf.h>
#include <hidport.h>

#include "HidReport.h"

struct TouchDeviceContext
{
    WDFQUEUE DefaultQueue = nullptr;
    ULONG ContactCount = 0;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TouchDeviceContext, GetTouchContext)

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL TouchEvtInternalIoctl;
NTSTATUS TouchDeviceCreate(_Inout_ PWDFDEVICE_INIT deviceInit);
