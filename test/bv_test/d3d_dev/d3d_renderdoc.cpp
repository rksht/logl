#include "d3d11_misc.h"

#include <learnogl/callstack.h>

using namespace fo;

namespace d3d11_misc {

// Load renderdoc
void load_renderdoc(WindowContext &d3d, const char *capture_path_template) {
    fs::path rdoc_dll_path(LOGL_RENDERDOC_DLL_PATH);
    auto pathstr = rdoc_dll_path.u8string();

    auto rdoc_dll = LoadLibrary(pathstr.c_str());
    CHECK_NE_F(rdoc_dll, (HINSTANCE)NULL, "Failed to load renderdoc.dll");
    auto RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress((HMODULE)rdoc_dll, "RENDERDOC_GetAPI");
    CHECK_NE_F(RENDERDOC_GetAPI, nullptr, "Failed to GetProcAddress RENDERDOC_GetAPI");

    int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void **>(&d3d.rdoc));
    CHECK_NE_F(ret, 0, "Failed to RENDERDOC_GetAPI");

    fs::path capture_path;

    if (capture_path_template == nullptr) {
        capture_path = fs::path(LOGL_SOURCE_DIR) / "captures" / "unspecified";
    } else {
        capture_path = fs::path(capture_path_template);
    }

    // d3d.rdoc->SetActiveWindow(d3d.dev.Get(), glfwGetWin32Window(d3d.window));

    auto str = capture_path.generic_string();
    d3d.rdoc->SetCaptureFilePathTemplate(str.c_str());

    auto capture_key = eRENDERDOC_Key_F12;
    d3d.rdoc->SetCaptureKeys(&capture_key, 1);

    // d3d.capture_in_progress = false;

    d3d.rdoc->SetCaptureOptionU32(eRENDERDOC_Option_AllowFullscreen, 1);
    d3d.rdoc->SetCaptureOptionU32(eRENDERDOC_Option_APIValidation, 1);
    d3d.rdoc->SetCaptureOptionU32(eRENDERDOC_Option_DebugDeviceMode, 1);
    d3d.rdoc->SetCaptureOptionU32(eRENDERDOC_Option_VerifyMapWrites, 1);

    d3d.rdoc->MaskOverlayBits(eRENDERDOC_Overlay_All, eRENDERDOC_Overlay_All);

    LOG_F(INFO, "Renderdoc loaded. Capture files are at - %s", d3d.rdoc->GetCaptureFilePathTemplate());
}

// Begin a capture.
void start_renderdoc_frame_capture(WindowContext &d3d) {
    if (!d3d.rdoc) {
        return;
    }

    auto device = d3d.dev.Get();
    auto native_window = glfwGetWin32Window(d3d.window);

    CHECK_NE_F(d3d.rdoc, nullptr, "Renderdoc was not loaded. Cannot capture.");
    CHECK_EQ_F(d3d.capture_in_progress, false, "Capture already in progress.");

    // DLOG_F(INFO, "Starting frame capture...");

    d3d.rdoc->StartFrameCapture(device, (void *)native_window);

    // d3d.capture_in_progress = true;
}

// End current capture.
void end_renderdoc_frame_capture(WindowContext &d3d) {
    if (!d3d.rdoc) {
        return;
    }

    auto device = d3d.dev.Get();
    auto native_window = glfwGetWin32Window(d3d.window);

    DLOG_F(INFO, "Stopping frame capture...");

    CHECK_NE_F(d3d.rdoc, nullptr, "Renderdoc was not loaded. Cannot capture.");
    // CHECK_EQ_F(d3d.capture_in_progress, true);

    int ret = d3d.rdoc->EndFrameCapture(device, (void *)native_window);
    if (ret != 1) {
        string_stream::Buffer ss(memory_globals::default_allocator());
        print_callstack(ss);
        CHECK_EQ_F(ret, 1, "EndFrameCapture failed - callstack -\n%s", string_stream::c_str(ss));
    }

    d3d.capture_in_progress = false;
}

// Returns if a frame is currently being captured
bool is_renderdoc_frame_capturing(WindowContext &d3d) {
    if (!d3d.rdoc) {
        return false;
    }

    return d3d.rdoc->IsFrameCapturing();
}

// Shutdown renderdoc
void shutdown_renderdoc(WindowContext &d3d) {
    if (!d3d.rdoc) {
        return;
    }

    d3d.rdoc->Shutdown();
}

void trigger_renderdoc_frame_capture(WindowContext &d3d, u32 num_frames) {
    if (d3d.rdoc == nullptr) {
        return;
    }
    if (num_frames == 1) {
        d3d.rdoc->TriggerCapture();
    } else {
        d3d.rdoc->TriggerMultiFrameCapture(num_frames);
    }
}

} // namespace d3d11_misc
