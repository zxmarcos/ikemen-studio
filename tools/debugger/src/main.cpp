// Ikemen GO debugger: a Win32 + OpenGL window for Dear ImGui. The debugger
// itself is in app.cpp; this file is the platform glue (window, input, GL).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <cfloat>
#include <cstdio>
#include <vector>

#include <GL/gl.h>

#include "app.h"
#include "imgui.h"
#include "imgui_impl_opengl2.h"

static HWND g_hwnd;
static HDC g_hdc;
static bool g_mouseTracked;
static int g_shotRequest = -1; // scripted tests: save the next frame as shot_<n>.bmp

// Writes the back buffer as a 24-bit BMP next to the exe (both are bottom-up rows).
static void SaveFrame(int n, int w, int h) {
    int stride = (w * 3 + 3) & ~3;
    std::vector<unsigned char> pixels((size_t)stride * h);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, 0x80E0 /* GL_BGR_EXT */, GL_UNSIGNED_BYTE, pixels.data());
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t *slash = wcsrchr(path, L'\\');
    swprintf(slash + 1, MAX_PATH - (slash + 1 - path), L"shot_%d.bmp", n);
    BITMAPFILEHEADER file = {0x4D42};
    BITMAPINFOHEADER info = {sizeof(info), w, h, 1, 24, BI_RGB};
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + (DWORD)pixels.size();
    if (FILE *f = _wfopen(path, L"wb")) {
        fwrite(&file, sizeof(file), 1, f);
        fwrite(&info, sizeof(info), 1, f);
        fwrite(pixels.data(), 1, pixels.size(), f);
        fclose(f);
    }
}

static ImGuiKey VirtualKeyToImGuiKey(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9') return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return (ImGuiKey)(ImGuiKey_F1 + (vk - VK_F1));
    switch (vk) {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_OEM_MINUS: return ImGuiKey_Minus;
    case VK_OEM_PLUS: return ImGuiKey_Equal;
    case VK_OEM_PERIOD: return ImGuiKey_Period;
    case VK_OEM_COMMA: return ImGuiKey_Comma;
    case VK_ADD: return ImGuiKey_KeypadAdd;
    case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
    default: return ImGuiKey_None;
    }
}

static void UpdateModifiers(ImGuiIO &io) {
    io.AddKeyEvent(ImGuiMod_Ctrl, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (GetKeyState(VK_MENU) & 0x8000) != 0);
}

static void MouseButton(HWND hwnd, int button, bool down) {
    ImGuiIO &io = ImGui::GetIO();
    if (down && GetCapture() == nullptr) SetCapture(hwnd);
    io.AddMouseButtonEvent(button, down);
    if (!down && !ImGui::IsAnyMouseDown() && GetCapture() == hwnd) ReleaseCapture();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui::GetCurrentContext() == nullptr) return DefWindowProcW(hwnd, msg, wParam, lParam);
    ImGuiIO &io = ImGui::GetIO();
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!g_mouseTracked) {
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g_mouseTracked = true;
        }
        io.AddMousePosEvent((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSELEAVE:
        g_mouseTracked = false;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: MouseButton(hwnd, 0, true); return 0;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: MouseButton(hwnd, 1, true); return 0;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: MouseButton(hwnd, 2, true); return 0;
    case WM_LBUTTONUP: MouseButton(hwnd, 0, false); return 0;
    case WM_RBUTTONUP: MouseButton(hwnd, 1, false); return 0;
    case WM_MBUTTONUP: MouseButton(hwnd, 2, false); return 0;
    case WM_MOUSEWHEEL:
        io.AddMouseWheelEvent(0, (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
        return 0;
    case WM_MOUSEHWHEEL:
        io.AddMouseWheelEvent(-(float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA, 0);
        return 0;
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: {
        bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        UpdateModifiers(io);
        ImGuiKey key = wParam == VK_RETURN && (HIWORD(lParam) & KF_EXTENDED) ? ImGuiKey_KeypadEnter
                                                                              : VirtualKeyToImGuiKey(wParam);
        if (key != ImGuiKey_None) io.AddKeyEvent(key, down);
        if (wParam == VK_CONTROL) io.AddKeyEvent(ImGuiKey_LeftCtrl, down);
        if (wParam == VK_SHIFT) io.AddKeyEvent(ImGuiKey_LeftShift, down);
        if (wParam == VK_MENU) io.AddKeyEvent(ImGuiKey_LeftAlt, down);
        // Keep Alt+F4; F10 alone would enter the (missing) menu bar.
        if ((msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) && wParam != VK_F10) break;
        return 0;
    }
    case WM_CHAR:
        if (wParam > 0 && wParam < 0x10000) io.AddInputCharacterUTF16((unsigned short)wParam);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        io.AddFocusEvent(msg == WM_SETFOCUS);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            LPCWSTR cursor = IDC_ARROW;
            switch (ImGui::GetMouseCursor()) {
            case ImGuiMouseCursor_TextInput: cursor = IDC_IBEAM; break;
            case ImGuiMouseCursor_ResizeAll: cursor = IDC_SIZEALL; break;
            case ImGuiMouseCursor_ResizeEW: cursor = IDC_SIZEWE; break;
            case ImGuiMouseCursor_ResizeNS: cursor = IDC_SIZENS; break;
            case ImGuiMouseCursor_ResizeNESW: cursor = IDC_SIZENESW; break;
            case ImGuiMouseCursor_ResizeNWSE: cursor = IDC_SIZENWSE; break;
            case ImGuiMouseCursor_Hand: cursor = IDC_HAND; break;
            case ImGuiMouseCursor_NotAllowed: cursor = IDC_NO; break;
            default: break;
            }
            SetCursor(LoadCursorW(nullptr, cursor));
            return TRUE;
        }
        break;
    case WM_APP + 0x4D47:
        // Scripted tests: wParam 0 move, 1/2 left down/up; lParam x | y << 16. Posted WM_MOUSE*
        // messages would be undone by WM_MOUSELEAVE, the real cursor being elsewhere.
        io.AddMousePosEvent((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam));
        if (wParam == 1 || wParam == 2) io.AddMouseButtonEvent(0, wParam == 1);
        return 0;
    case WM_APP + 0x4D48: // scripted tests: save the next frame as shot_<wParam>.bmp
        g_shotRequest = (int)wParam;
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool CreateGLContext() {
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1};
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    g_hdc = GetDC(g_hwnd);
    int format = ChoosePixelFormat(g_hdc, &pfd);
    if (!format || !SetPixelFormat(g_hdc, format, &pfd)) return false;
    HGLRC rc = wglCreateContext(g_hdc);
    if (!rc || !wglMakeCurrent(g_hdc, rc)) return false;
    typedef BOOL(WINAPI * SwapIntervalFn)(int);
    if (auto swapInterval = (SwapIntervalFn)wglGetProcAddress("wglSwapIntervalEXT")) swapInterval(1);
    return true;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmdLine, int show) {
    SetProcessDPIAware();
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"IkemenDebugger";
    RegisterClassExW(&wc);

    HDC screen = GetDC(nullptr);
    float scale = GetDeviceCaps(screen, LOGPIXELSX) / 96.0f;
    ReleaseDC(nullptr, screen);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Ikemen GO debugger", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                             CW_USEDEFAULT, (int)(1440 * scale), (int)(900 * scale), nullptr, nullptr, inst, nullptr);
    if (!g_hwnd || !CreateGLContext()) {
        MessageBoxW(nullptr, L"Could not create an OpenGL window.", L"Ikemen GO debugger", MB_ICONERROR);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.BackendPlatformName = "ikemen_debugger_win32";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetStyle().FontScaleDpi = scale;
    ImGui_ImplOpenGL2_Init();
    App_Init(wcsstr(cmdLine, L"--launch") != nullptr);

    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        if (IsIconic(g_hwnd)) {
            App_Poll(); // keep draining the game's output
            Sleep(16);
            continue;
        }

        RECT rc;
        GetClientRect(g_hwnd, &rc);
        io.DisplaySize = ImVec2((float)(rc.right - rc.left), (float)(rc.bottom - rc.top));
        QueryPerformanceCounter(&now);
        io.DeltaTime = (float)(now.QuadPart - last.QuadPart) / freq.QuadPart;
        if (io.DeltaTime <= 0) io.DeltaTime = 1.0f / 1000;
        last = now;

        ImGui_ImplOpenGL2_NewFrame();
        ImGui::NewFrame();
        App_Frame();
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.08f, 0.08f, 0.09f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        if (g_shotRequest >= 0) {
            SaveFrame(g_shotRequest, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
            g_shotRequest = -1;
        }
        SwapBuffers(g_hdc);
    }

    App_Shutdown();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
