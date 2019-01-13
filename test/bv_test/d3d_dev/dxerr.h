// Not compatible with the official dxerr.h. wchar_t is not used, all functions take and return char *
// strings.

#pragma once

#include <sal.h>
#include <windows.h>

#if defined(DXERR_LOGURU)
#define LOGURU_WITH_STREAMS 1
#include <loguru.hpp>
#endif

#ifdef __cplusplus
extern "C" {
#endif

HRESULT WINAPI dxlog(_In_z_ const CHAR *filename,
                     _In_ int line,
                     _In_ HRESULT hr,
                     _In_opt_ const CHAR *strMsg,
                     _In_ bool pop_msg_box);

// -- Helper macros

#if defined(DEBUG) || defined(_DEBUG)
#define DXLOG_MSG(str) dxlog(__FILE__, __LINE__, 0, str, false)
#define DXLOG_ERR(str, hr) dxlog(__FILE__, __LINE__, hr, str, false)
#define DXLOG_ERR_MSGBOX(str, hr) dxlog(__FILE__, __LINE__, hr, str, true)

#else
#define DXLOG_MSG(str) (0L)
#define DXLOG_ERR(str, hr) (hr)
#define DXLOG_ERR_MSGBOX(str, hr) (hr)

#endif

#ifdef __cplusplus
}
#endif //__cplusplus
