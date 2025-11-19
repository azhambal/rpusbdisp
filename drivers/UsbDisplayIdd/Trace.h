#pragma once

//
// Define WPP trace control GUID:
// {2B4E8F6A-9C3D-4B1E-A7F8-5D9E2C4B6A1F}
//
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID( \
        UsbDisplayIddTraceGuid, (2B4E8F6A,9C3D,4B1E,A7F8,5D9E2C4B6A1F), \
        WPP_DEFINE_BIT(TRACE_DRIVER)     /* bit  0 = 0x00000001 */ \
        WPP_DEFINE_BIT(TRACE_DEVICE)     /* bit  1 = 0x00000002 */ \
        WPP_DEFINE_BIT(TRACE_DISPLAY)    /* bit  2 = 0x00000004 */ \
        WPP_DEFINE_BIT(TRACE_PIPELINE)   /* bit  3 = 0x00000008 */ \
        WPP_DEFINE_BIT(TRACE_SWAPCHAIN)  /* bit  4 = 0x00000010 */ \
        WPP_DEFINE_BIT(TRACE_PRESENT)    /* bit  5 = 0x00000020 */ \
        )

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

//
// This comment block is scanned by the trace preprocessor to define the
// TraceEvents function and associated events:
//

// begin_wpp config
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp

//
// Trace macros for common patterns
//

#define TRACE_FUNCTION_ENTRY(flags) \
    TraceEvents(TRACE_LEVEL_VERBOSE, flags, "%!FUNC! Entry")

#define TRACE_FUNCTION_EXIT(flags) \
    TraceEvents(TRACE_LEVEL_VERBOSE, flags, "%!FUNC! Exit")

#define TRACE_FUNCTION_EXIT_NTSTATUS(flags, status) \
    TraceEvents(TRACE_LEVEL_VERBOSE, flags, "%!FUNC! Exit: %!STATUS!", status)

#define TRACE_ERROR(flags, msg, ...) \
    TraceEvents(TRACE_LEVEL_ERROR, flags, msg, __VA_ARGS__)

#define TRACE_WARNING(flags, msg, ...) \
    TraceEvents(TRACE_LEVEL_WARNING, flags, msg, __VA_ARGS__)

#define TRACE_INFO(flags, msg, ...) \
    TraceEvents(TRACE_LEVEL_INFORMATION, flags, msg, __VA_ARGS__)

#define TRACE_VERBOSE(flags, msg, ...) \
    TraceEvents(TRACE_LEVEL_VERBOSE, flags, msg, __VA_ARGS__)
