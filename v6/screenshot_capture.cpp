// screenshot_capture.cpp
// 基于Win32 API的截屏程序，支持全局热键Ctrl+PrtSc和Ctrl+F1
// 修复高DPI屏幕截图问题 - 真正获取物理分辨率
// 编译: g++ -mwindows -o screenshot_capture.exe screenshot_capture.cpp -lgdi32 -lcomctl32 -lgdiplus -ld2d1

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <memory>

using namespace Gdiplus;

// 控件ID定义
#define IDC_LOG_LIST      1001
#define IDC_STATUS_TEXT   1002
#define IDM_HOTKEY_PRINTSCREEN    2001  // Ctrl+PrtSc 热键ID
#define IDM_HOTKEY_F1             2002  // Ctrl+F1 热键ID

// 全局变量
HWND g_hMainWnd = NULL;
HWND g_hLogList = NULL;
std::mutex g_logMutex;
HHOOK g_hKeyboardHook = NULL;
bool g_bInBackground = false;
ULONG_PTR g_gdiplusToken = NULL;

// 日志级别枚举
enum LogLevel {
    LOG_INFO = 0,
    LOG_WARNING,
    LOG_ERROR,
    LOG_DEBUG
};

// 获取当前时间字符串（用于文件名）
std::wstring GetCurrentTimeString() {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    wchar_t buffer[64];
    wcsftime(buffer, 64, L"%Y%m%d %H-%M-%S", t);
    return std::wstring(buffer);
}

// 获取当前时间字符串（用于日志）
std::wstring GetCurrentTimeStringForLog() {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    wchar_t buffer[64];
    wcsftime(buffer, 64, L"[%Y-%m-%d %H:%M:%S] ", t);
    return std::wstring(buffer);
}

// 日志写入函数
void WriteLog(LogLevel level, const std::wstring& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    
    std::wstring levelStr;
    switch(level) {
        case LOG_INFO: levelStr = L"[INFO] "; break;
        case LOG_WARNING: levelStr = L"[WARNING] "; break;
        case LOG_ERROR: levelStr = L"[ERROR] "; break;
        case LOG_DEBUG: levelStr = L"[DEBUG] "; break;
        default: levelStr = L"[UNKNOWN] "; break;
    }
    
    std::wstring logEntry = GetCurrentTimeStringForLog() + levelStr + message;
    
    // 输出到调试器
    OutputDebugStringW((logEntry + L"\n").c_str());
    
    // 添加到列表控件
    if (g_hLogList && IsWindow(g_hLogList)) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = SendMessageW(g_hLogList, LVM_GETITEMCOUNT, 0, 0);
        lvi.iSubItem = 0;
        lvi.pszText = (LPWSTR)logEntry.c_str();
        SendMessageW(g_hLogList, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
        
        // 自动滚动到底部
        SendMessageW(g_hLogList, LVM_ENSUREVISIBLE, lvi.iItem, FALSE);
        
        // 限制日志条目数量，避免内存过大（保留10000条）
        int count = SendMessageW(g_hLogList, LVM_GETITEMCOUNT, 0, 0);
        if (count > 10000) {
            SendMessageW(g_hLogList, LVM_DELETEITEM, 0, 0);
        }
    }
}

// 确保目录存在
bool EnsureDirectoryExists(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), NULL)) {
        WriteLog(LOG_INFO, L"创建目录: " + path);
        return true;
    }
    DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS) {
        return true;
    }
    WriteLog(LOG_WARNING, L"创建目录失败，错误代码: " + std::to_wstring(error));
    return false;
}

// 获取屏幕的实际物理尺寸（通过EnumDisplaySettings）
void GetScreenPhysicalSize(int& width, int& height) {
    DEVMODE devMode ; //= {0};
    ZeroMemory(&devMode, sizeof(DEVMODE));
    devMode.dmSize = sizeof(DEVMODE);
    
    if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &devMode)) {
        width = devMode.dmPelsWidth;
        height = devMode.dmPelsHeight;
        WriteLog(LOG_DEBUG, L"通过EnumDisplaySettings获取物理分辨率: " + 
                 std::to_wstring(width) + L"x" + std::to_wstring(height));
        return;
    }
    
    // 降级方案：使用GetDeviceCaps
    HDC hdc = GetDC(NULL);
    width = GetDeviceCaps(hdc, HORZRES);
    height = GetDeviceCaps(hdc, VERTRES);
    ReleaseDC(NULL, hdc);
    
    WriteLog(LOG_DEBUG, L"通过GetDeviceCaps获取物理分辨率: " + 
             std::to_wstring(width) + L"x" + std::to_wstring(height));
}


// 将HBITMAP保存为JPEG
bool SaveBitmapToJPEG(HBITMAP hBitmap, const std::wstring& filePath, int quality = 90) {
    bool success = false;
    
    try {
        // 创建Bitmap对象
        Bitmap* bitmap = Bitmap::FromHBITMAP(hBitmap, NULL);
        if (!bitmap || bitmap->GetLastStatus() != Ok) {
            WriteLog(LOG_ERROR, L"创建GDI+ Bitmap对象失败");
            if (bitmap) delete bitmap;
            return false;
        }
        
        // 获取图像编码器信息（JPEG）
        CLSID jpegClsid;
        UINT numEncoders = 0;
        UINT size = 0;
        
        GetImageEncodersSize(&numEncoders, &size);
        if (size == 0) {
            WriteLog(LOG_ERROR, L"未找到图像编码器");
            delete bitmap;
            return false;
        }
        
        std::unique_ptr<ImageCodecInfo[]> codecInfo(new ImageCodecInfo[size]);
        GetImageEncoders(numEncoders, size, codecInfo.get());
        
        bool found = false;
        for (UINT i = 0; i < numEncoders; i++) {
            if (wcscmp(codecInfo[i].MimeType, L"image/jpeg") == 0) {
                jpegClsid = codecInfo[i].Clsid;
                found = true;
                break;
            }
        }
        
        if (!found) {
            WriteLog(LOG_ERROR, L"未找到JPEG编码器");
            delete bitmap;
            return false;
        }
        
        // 设置JPEG质量参数
        EncoderParameters encoderParams;
        encoderParams.Count = 1;
        encoderParams.Parameter[0].Guid = EncoderQuality;
        encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
        encoderParams.Parameter[0].NumberOfValues = 1;
        
        ULONG qualityValue = quality;
        encoderParams.Parameter[0].Value = &qualityValue;
        
        // 保存为JPEG
        Status status = bitmap->Save(filePath.c_str(), &jpegClsid, &encoderParams);
        if (status == Ok) {
            success = true;
            WriteLog(LOG_DEBUG, L"JPEG质量设置: " + std::to_wstring(quality));
        } else {
            WriteLog(LOG_ERROR, L"保存JPEG失败，错误码: " + std::to_wstring(status));
        }
        
        delete bitmap;
        
    } catch (std::exception& e) {
        WriteLog(LOG_ERROR, L"保存JPEG时发生异常");
        success = false;
    }
    
    return success;
}

// 方法1：使用StretchBlt从逻辑屏幕拉伸到物理尺寸
bool CaptureScreenStretchBlt(const std::wstring& filePath) {
    WriteLog(LOG_INFO, L"尝试方法1: StretchBlt拉伸方式...");
    
    HDC hdcScreen = NULL;
    HDC hdcMem = NULL;
    HBITMAP hBitmap = NULL;
    bool success = false;
    
    try {
        // 获取物理尺寸
        int physicalWidth, physicalHeight;
        GetScreenPhysicalSize(physicalWidth, physicalHeight);
        
        // 获取逻辑尺寸
        int logicalWidth = GetSystemMetrics(SM_CXSCREEN);
        int logicalHeight = GetSystemMetrics(SM_CYSCREEN);
        
        WriteLog(LOG_INFO, L"逻辑分辨率: " + std::to_wstring(logicalWidth) + L"x" + std::to_wstring(logicalHeight));
        WriteLog(LOG_INFO, L"物理分辨率: " + std::to_wstring(physicalWidth) + L"x" + std::to_wstring(physicalHeight));
        
        // 获取屏幕DC
        hdcScreen = GetDC(NULL);
        if (!hdcScreen) {
            WriteLog(LOG_ERROR, L"获取屏幕DC失败");
            return false;
        }
        
        // 创建兼容DC
        hdcMem = CreateCompatibleDC(hdcScreen);
        if (!hdcMem) {
            WriteLog(LOG_ERROR, L"创建内存DC失败");
            ReleaseDC(NULL, hdcScreen);
            return false;
        }
        
        // 创建物理尺寸的位图
        hBitmap = CreateCompatibleBitmap(hdcScreen, physicalWidth, physicalHeight);
        if (!hBitmap) {
            WriteLog(LOG_ERROR, L"创建位图失败");
            DeleteDC(hdcMem);
            ReleaseDC(NULL, hdcScreen);
            return false;
        }
        
        // 选择位图到内存DC
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
        
        // 使用StretchBlt从逻辑尺寸拉伸到物理尺寸
        // 设置高质量拉伸模式
        SetStretchBltMode(hdcMem, HALFTONE);
        
        if (!StretchBlt(hdcMem, 0, 0, physicalWidth, physicalHeight,
                       hdcScreen, 0, 0, logicalWidth, logicalHeight, SRCCOPY)) {
            WriteLog(LOG_ERROR, L"StretchBlt失败");
            SelectObject(hdcMem, hOldBitmap);
            DeleteObject(hBitmap);
            DeleteDC(hdcMem);
            ReleaseDC(NULL, hdcScreen);
            return false;
        }
        
        WriteLog(LOG_INFO, L"StretchBlt成功，已拉伸到物理尺寸");
        
        // 保存为JPEG
        if (SaveBitmapToJPEG(hBitmap, filePath, 90)) {
            WriteLog(LOG_INFO, L"截图成功保存: " + filePath);
            success = true;
        } else {
            WriteLog(LOG_ERROR, L"保存JPEG失败");
        }
        
        // 清理资源
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        
    } catch (...) {
        WriteLog(LOG_ERROR, L"截屏过程中发生异常");
        if (hBitmap) DeleteObject(hBitmap);
        if (hdcMem) DeleteDC(hdcMem);
        if (hdcScreen) ReleaseDC(NULL, hdcScreen);
    }
    
    return success;
}

// 方法2：使用DirectX方法获取屏幕（通过桌面窗口DC和物理尺寸）
bool CaptureScreenDirectX(const std::wstring& filePath) {
    WriteLog(LOG_INFO, L"尝试方法2: 桌面窗口DC + 物理尺寸方式...");
    
    HDC hdcScreen = NULL;
    HDC hdcMem = NULL;
    HBITMAP hBitmap = NULL;
    HBITMAP hOldBitmap = NULL;
    bool success = false;
    
    try {
        // 获取物理尺寸
        int physicalWidth, physicalHeight;
        GetScreenPhysicalSize(physicalWidth, physicalHeight);
        
        // 获取桌面窗口
        HWND hDesktopWnd = GetDesktopWindow();
        HDC hdcDesktop = GetDCEx(hDesktopWnd, NULL, DCX_CACHE | DCX_LOCKWINDOWUPDATE);
        
        if (!hdcDesktop) {
            // 降级使用普通DC
            hdcScreen = GetDC(NULL);
        } else {
            hdcScreen = hdcDesktop;
        }
        
        if (!hdcScreen) {
            WriteLog(LOG_ERROR, L"获取桌面DC失败");
            return false;
        }
        
        // 创建兼容DC
        hdcMem = CreateCompatibleDC(hdcScreen);
        if (!hdcMem) {
            WriteLog(LOG_ERROR, L"创建内存DC失败");
            if (hdcDesktop) ReleaseDC(hDesktopWnd, hdcDesktop);
            if (hdcScreen) ReleaseDC(NULL, hdcScreen);
            return false;
        }
        
        // 创建物理尺寸的位图
        hBitmap = CreateCompatibleBitmap(hdcScreen, physicalWidth, physicalHeight);
        if (!hBitmap) {
            WriteLog(LOG_ERROR, L"创建位图失败");
            DeleteDC(hdcMem);
            if (hdcDesktop) ReleaseDC(hDesktopWnd, hdcDesktop);
            if (hdcScreen) ReleaseDC(NULL, hdcScreen);
            return false;
        }
        
        // 选择位图到内存DC
        hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
        
        // 复制屏幕内容 - 直接使用物理尺寸
        // 注意：这里假设屏幕左上角是(0,0)
        if (!BitBlt(hdcMem, 0, 0, physicalWidth, physicalHeight,
                   hdcScreen, 0, 0, SRCCOPY)) {
            
            WriteLog(LOG_WARNING, L"BitBlt失败，尝试使用PrintWindow...");
            
            // 备用方案：使用PrintWindow
            if (!PrintWindow(GetDesktopWindow(), hdcMem, PW_CLIENTONLY)) {
                WriteLog(LOG_ERROR, L"PrintWindow也失败");
                SelectObject(hdcMem, hOldBitmap);
                DeleteObject(hBitmap);
                DeleteDC(hdcMem);
                if (hdcDesktop) ReleaseDC(hDesktopWnd, hdcDesktop);
                if (hdcScreen) ReleaseDC(NULL, hdcScreen);
                return false;
            }
        }
        
        WriteLog(LOG_INFO, L"成功捕获屏幕，物理尺寸: " + 
                 std::to_wstring(physicalWidth) + L"x" + std::to_wstring(physicalHeight));
        
        // 保存为JPEG
        if (SaveBitmapToJPEG(hBitmap, filePath, 90)) {
            WriteLog(LOG_INFO, L"截图成功保存: " + filePath);
            success = true;
        } else {
            WriteLog(LOG_ERROR, L"保存JPEG失败");
        }
        
        // 清理资源
        if (hOldBitmap) SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        if (hdcDesktop) ReleaseDC(hDesktopWnd, hdcDesktop);
        if (hdcScreen && !hdcDesktop) ReleaseDC(NULL, hdcScreen);
        
    } catch (...) {
        WriteLog(LOG_ERROR, L"截屏过程中发生异常");
        if (hBitmap) DeleteObject(hBitmap);
        if (hdcMem) DeleteDC(hdcMem);
        if (hdcScreen) ReleaseDC(NULL, hdcScreen);
    }
    
    return success;
}
// 方法3：使用GDI+直接截取（通过Graphics对象）
bool CaptureScreenGdiPlus(const std::wstring& filePath) {
    WriteLog(LOG_INFO, L"尝试方法3: GDI+ Graphics方式...");
    
    bool success = false;
    
    try {
        // 获取物理尺寸
        int physicalWidth, physicalHeight;
        GetScreenPhysicalSize(physicalWidth, physicalHeight);
        
        // 获取逻辑尺寸
        int logicalWidth = GetSystemMetrics(SM_CXSCREEN);
        int logicalHeight = GetSystemMetrics(SM_CYSCREEN);
        
        WriteLog(LOG_DEBUG, L"GDI+方法 - 逻辑尺寸: " + std::to_wstring(logicalWidth) + 
                 L"x" + std::to_wstring(logicalHeight));
        WriteLog(LOG_DEBUG, L"GDI+方法 - 物理尺寸: " + std::to_wstring(physicalWidth) + 
                 L"x" + std::to_wstring(physicalHeight));
        
        // 获取屏幕DC
        HDC hdcScreen = GetDC(NULL);
        if (!hdcScreen) {
            WriteLog(LOG_ERROR, L"获取屏幕DC失败");
            return false;
        }
        
        // 创建GDI+ Bitmap对象（物理尺寸）
        Bitmap* bitmap = new Bitmap(physicalWidth, physicalHeight, PixelFormat32bppARGB);
        if (!bitmap || bitmap->GetLastStatus() != Ok) {
            WriteLog(LOG_ERROR, L"创建GDI+ Bitmap失败");
            if (bitmap) delete bitmap;
            ReleaseDC(NULL, hdcScreen);
            return false;
        }
        
        // 获取Bitmap的Graphics对象
        Graphics graphics(bitmap);
        
        // 设置高质量插值模式
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(SmoothingModeHighQuality);
        
        // 创建源屏幕的Bitmap（逻辑尺寸）
        HDC hdcMem = CreateCompatibleDC(hdcScreen);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, logicalWidth, logicalHeight);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
        
        // 复制屏幕内容到临时位图
        if (!BitBlt(hdcMem, 0, 0, logicalWidth, logicalHeight, hdcScreen, 0, 0, SRCCOPY)) {
            WriteLog(LOG_WARNING, L"BitBlt失败");
        }
        
        // 从临时位图创建GDI+ Bitmap
        Bitmap screenBitmap(hBitmap, NULL);
        
        // 将逻辑尺寸的图像绘制到物理尺寸的Bitmap（拉伸）
        Rect destRect(0, 0, physicalWidth, physicalHeight);
        graphics.DrawImage(&screenBitmap, destRect, 0, 0, logicalWidth, logicalHeight, UnitPixel);
        
        // 清理临时资源
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        
        // 获取JPEG编码器
        CLSID jpegClsid;
        UINT numEncoders = 0;
        UINT size = 0;
        
        GetImageEncodersSize(&numEncoders, &size);
        if (size == 0) {
            WriteLog(LOG_ERROR, L"未找到图像编码器");
            delete bitmap;
            return false;
        }
        
        std::unique_ptr<ImageCodecInfo[]> codecInfo(new ImageCodecInfo[size]);
        GetImageEncoders(numEncoders, size, codecInfo.get());
        
        bool found = false;
        for (UINT i = 0; i < numEncoders; i++) {
            if (wcscmp(codecInfo[i].MimeType, L"image/jpeg") == 0) {
                jpegClsid = codecInfo[i].Clsid;
                found = true;
                break;
            }
        }
        
        if (!found) {
            WriteLog(LOG_ERROR, L"未找到JPEG编码器");
            delete bitmap;
            return false;
        }
        
        // 设置JPEG质量
        EncoderParameters encoderParams;
        encoderParams.Count = 1;
        encoderParams.Parameter[0].Guid = EncoderQuality;
        encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
        encoderParams.Parameter[0].NumberOfValues = 1;
        ULONG quality = 90;
        encoderParams.Parameter[0].Value = &quality;
        
        // 保存为JPEG
        if (bitmap->Save(filePath.c_str(), &jpegClsid, &encoderParams) == Ok) {
            WriteLog(LOG_INFO, L"截图成功保存: " + filePath);
            success = true;
        } else {
            WriteLog(LOG_ERROR, L"保存JPEG失败");
        }
        
        delete bitmap;
        
    } catch (const std::exception& e) {
        WriteLog(LOG_ERROR, L"GDI+截屏发生异常: " + std::wstring(e.what(), e.what() + strlen(e.what())));
        success = false;
    } catch (...) {
        WriteLog(LOG_ERROR, L"GDI+截屏发生未知异常");
        success = false;
    }
    
    return success;
}

// 主截屏函数 - 尝试多种方法
bool CaptureAllScreens(const std::wstring& filePath) {
    WriteLog(LOG_INFO, L"开始截屏操作，尝试多种方法获取物理分辨率...");
    
    // 方法2: 桌面窗口DC方式（备用）
    WriteLog(LOG_INFO, L"尝试桌面窗口DC方式...");
    if (CaptureScreenDirectX(filePath)) {
        return true;
    }
    // 方法3: GDI+方式
    WriteLog(LOG_INFO, L"桌面窗口DC方式失败，尝试GDI+方式...");
    if (CaptureScreenGdiPlus(filePath)) {
        return true;
    }
    
    // 方法1: StretchBlt拉伸方式（最可靠，适用于高DPI）
    if (CaptureScreenStretchBlt(filePath)) {
        return true;
    }
    
    
    WriteLog(LOG_ERROR, L"所有截屏方法均失败");
    return false;
}

// 执行截屏操作（从热键调用）
void PerformScreenshot(const std::wstring& triggerKey) {
    WriteLog(LOG_INFO, L"检测到热键: " + triggerKey);
    
    // 确保目录存在
    std::wstring dirPath = L"c:\\screenshots";
    if (!EnsureDirectoryExists(dirPath)) {
        WriteLog(LOG_ERROR, L"无法创建或访问目录: " + dirPath);
        MessageBeep(MB_ICONERROR);
        return;
    }
    
    // 生成文件名（JPEG格式）
    std::wstring fileName = GetCurrentTimeString() + L".jpg";
    std::wstring fullPath = dirPath + L"\\" + fileName;
    
    WriteLog(LOG_DEBUG, L"文件路径: " + fullPath);
    
    // 执行截屏（截取所有显示器）
    if (CaptureAllScreens(fullPath)) {
        // 播放成功提示音
        MessageBeep(MB_OK);
        
        // 如果窗口可见，闪一下提示
        if (g_hMainWnd && IsWindow(g_hMainWnd) && IsWindowVisible(g_hMainWnd)) {
            FLASHWINFO fi = {0};
            fi.cbSize = sizeof(FLASHWINFO);
            fi.hwnd = g_hMainWnd;
            fi.dwFlags = FLASHW_CAPTION | FLASHW_TRAY;
            fi.uCount = 3;
            fi.dwTimeout = 100;
            FlashWindowEx(&fi);
        }
    } else {
        WriteLog(LOG_ERROR, L"截屏失败");
        MessageBeep(MB_ICONERROR);
    }
}

// 全局键盘钩子过程（用于后台捕获）
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKbStruct = (KBDLLHOOKSTRUCT*)lParam;
        
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            // 检查Ctrl键是否按下
            bool bCtrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            
            if (bCtrlPressed) {
                // 检查 Ctrl+PrtSc
                if (pKbStruct->vkCode == VK_SNAPSHOT) {
                    WriteLog(LOG_DEBUG, L"捕获到键盘事件: Ctrl+PrtSc");
                    PostMessage(g_hMainWnd, WM_COMMAND, IDM_HOTKEY_PRINTSCREEN, 0);
                    return 1;  // 阻止系统默认的PrtSc行为
                }
                // 检查 Ctrl+F1 (VK_F1 = 0x70)
                else if (pKbStruct->vkCode == VK_F1) {
                    WriteLog(LOG_DEBUG, L"捕获到键盘事件: Ctrl+F1");
                    PostMessage(g_hMainWnd, WM_COMMAND, IDM_HOTKEY_F1, 0);
                    return 1;  // 阻止F1的默认帮助行为
                }
            }
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

// 注册系统热键（备用方案）
bool RegisterHotKeyAlternate() {
    bool bSuccess = false;
    std::wstring registeredKeys;
    
    // 尝试注册热键 Ctrl+PrtSc
    if (RegisterHotKey(g_hMainWnd, IDM_HOTKEY_PRINTSCREEN, MOD_CONTROL, VK_SNAPSHOT)) {
        WriteLog(LOG_INFO, L"系统热键注册成功: Ctrl+PrtSc");
        registeredKeys += L"Ctrl+PrtSc ";
        bSuccess = true;
    } else {
        DWORD error = GetLastError();
        WriteLog(LOG_WARNING, L"系统热键注册失败 Ctrl+PrtSc，错误代码: " + std::to_wstring(error));
    }
    
    // 尝试注册热键 Ctrl+F1
    if (RegisterHotKey(g_hMainWnd, IDM_HOTKEY_F1, MOD_CONTROL, VK_F1)) {
        WriteLog(LOG_INFO, L"系统热键注册成功: Ctrl+F1");
        registeredKeys += L"Ctrl+F1";
        bSuccess = true;
    } else {
        DWORD error = GetLastError();
        WriteLog(LOG_WARNING, L"系统热键注册失败 Ctrl+F1，错误代码: " + std::to_wstring(error));
    }
    
    if (!bSuccess) {
        WriteLog(LOG_WARNING, L"所有系统热键注册失败，将使用全局键盘钩子");
    }
    
    return bSuccess;
}

// 检查是否已有实例在运行，如果有则激活并返回true
bool CheckAndActivateExistingInstance() {
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"ScreenshotCapture_Mutex");
    DWORD dwError = GetLastError();
    
    if (dwError == ERROR_ALREADY_EXISTS) {
        // 已有实例在运行
        WriteLog(LOG_INFO, L"检测到已有程序实例在运行");
        
        // 查找已存在的窗口
        HWND hExistingWnd = FindWindowW(L"ScreenshotCaptureClass", L"截图捕获程序");
        if (hExistingWnd && IsWindow(hExistingWnd)) {
            WriteLog(LOG_INFO, L"找到已存在的窗口，将其激活");
            
            // 如果窗口最小化，先恢复
            if (IsIconic(hExistingWnd)) {
                ShowWindow(hExistingWnd, SW_RESTORE);
            }
            
            // 将窗口带到前台
            SetForegroundWindow(hExistingWnd);
            
            // 闪烁窗口提示用户
            FLASHWINFO fi = {0};
            fi.cbSize = sizeof(FLASHWINFO);
            fi.hwnd = hExistingWnd;
            fi.dwFlags = FLASHW_CAPTION | FLASHW_TRAY;
            fi.uCount = 5;
            fi.dwTimeout = 200;
            FlashWindowEx(&fi);
            
            WriteLog(LOG_INFO, L"已激活现有窗口");
        } else {
            WriteLog(LOG_WARNING, L"找到互斥体但未找到窗口");
        }
        
        CloseHandle(hMutex);
        return true;  // 已有实例，不需要创建新实例
    }
    
    // 互斥体创建成功，保持打开直到程序结束
    return false;  // 没有其他实例，可以继续运行
}

// 窗口过程
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            {
                // 创建日志列表
                INITCOMMONCONTROLSEX icex;
                icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
                icex.dwICC = ICC_LISTVIEW_CLASSES;
                InitCommonControlsEx(&icex);
                
                RECT rcClient;
                GetClientRect(hWnd, &rcClient);
                
                g_hLogList = CreateWindowW(WC_LISTVIEWW, L"",
                    WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_NOSORTHEADER,
                    10, 10, rcClient.right - 20, rcClient.bottom - 60,
                    hWnd, (HMENU)IDC_LOG_LIST, GetModuleHandle(NULL), NULL);
                
                if (g_hLogList) {
                    // 设置列
                    LVCOLUMNW col = {0};
                    col.mask = LVCF_WIDTH | LVCF_TEXT;
                    col.cx = rcClient.right - 30;
                    col.pszText = L"操作日志";
                    ListView_InsertColumn(g_hLogList, 0, &col);
                    
                    WriteLog(LOG_INFO, L"程序启动");
                    WriteLog(LOG_INFO, L"日志控件初始化成功");
                    WriteLog(LOG_INFO, L"支持的热键: Ctrl+PrtSc 和 Ctrl+F1");
                    WriteLog(LOG_INFO, L"截图格式: JPEG (质量90%)");
                    
                    // 显示DPI信息
                    HDC hdc = GetDC(NULL);
                    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
                    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
                    ReleaseDC(NULL, hdc);
                    WriteLog(LOG_INFO, L"系统DPI: " + std::to_wstring(dpiX) + L"x" + std::to_wstring(dpiY));
                    
                    int physicalWidth, physicalHeight;
                    GetScreenPhysicalSize(physicalWidth, physicalHeight);
                    WriteLog(LOG_INFO, L"物理分辨率: " + std::to_wstring(physicalWidth) + 
                             L"x" + std::to_wstring(physicalHeight));
                    
                    int logicalWidth = GetSystemMetrics(SM_CXSCREEN);
                    int logicalHeight = GetSystemMetrics(SM_CYSCREEN);
                    WriteLog(LOG_INFO, L"逻辑分辨率: " + std::to_wstring(logicalWidth) + 
                             L"x" + std::to_wstring(logicalHeight));
                    
                    float scale = (float)physicalWidth / logicalWidth;
                    WriteLog(LOG_INFO, L"缩放比例: " + std::to_wstring(scale));
                } else {
                    WriteLog(LOG_ERROR, L"创建日志控件失败");
                }
                
                // 创建状态文本
                CreateWindowW(L"STATIC", L"程序运行中... 按 Ctrl+PrtSc 或 Ctrl+F1 截屏 (支持高DPI物理分辨率)",
                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                    10, rcClient.bottom - 40, rcClient.right - 20, 25,
                    hWnd, (HMENU)IDC_STATUS_TEXT, GetModuleHandle(NULL), NULL);
            }
            break;
            
        case WM_SIZE:
            {
                // 调整控件大小
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);
                if (g_hLogList) {
                    SetWindowPos(g_hLogList, NULL, 10, 10, width - 20, height - 60, SWP_NOZORDER);
                    
                    // 调整列宽
                    ListView_SetColumnWidth(g_hLogList, 0, width - 30);
                }
                HWND hStatus = GetDlgItem(hWnd, IDC_STATUS_TEXT);
                if (hStatus) {
                    SetWindowPos(hStatus, NULL, 10, height - 40, width - 20, 25, SWP_NOZORDER);
                }
            }
            break;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_HOTKEY_PRINTSCREEN:
                    PerformScreenshot(L"Ctrl+PrtSc");
                    break;
                case IDM_HOTKEY_F1:
                    PerformScreenshot(L"Ctrl+F1");
                    break;
            }
            break;
            
        case WM_HOTKEY:
            switch (wParam) {
                case IDM_HOTKEY_PRINTSCREEN:
                    PerformScreenshot(L"Ctrl+PrtSc");
                    break;
                case IDM_HOTKEY_F1:
                    PerformScreenshot(L"Ctrl+F1");
                    break;
            }
            break;
            
        case WM_DESTROY:
            {
                WriteLog(LOG_INFO, L"程序关闭");
                
                // 注销热键
                UnregisterHotKey(hWnd, IDM_HOTKEY_PRINTSCREEN);
                UnregisterHotKey(hWnd, IDM_HOTKEY_F1);
                
                // 卸载键盘钩子
                if (g_hKeyboardHook) {
                    UnhookWindowsHookEx(g_hKeyboardHook);
                    g_hKeyboardHook = NULL;
                }
                
                PostQuitMessage(0);
            }
            break;
            
        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

// 注册窗口类
bool RegisterWindowClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcex = {0};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = L"ScreenshotCaptureClass";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    
    return RegisterClassExW(&wcex) != 0;
}

// 命令行参数解析
void ParseCommandLine() {
    LPWSTR cmdLine = GetCommandLineW();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--background") == 0 || wcscmp(argv[i], L"-b") == 0) {
            g_bInBackground = true;
            WriteLog(LOG_DEBUG, L"后台模式启动");
        }
    }
    
    if (argv) LocalFree(argv);
}

// 程序入口点
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 设置Unicode环境
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    
    // 检查是否已有实例在运行
    if (CheckAndActivateExistingInstance()) {
        // 已有实例，直接退出
        return 0;
    }
    
    // 初始化GDI+
    GdiplusStartupInput gdiplusStartupInput;
    if (GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        MessageBoxW(NULL, L"GDI+初始化失败，程序无法运行", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    // 初始化日志系统
    WriteLog(LOG_INFO, L"=========================================");
    WriteLog(LOG_INFO, L"截图捕获程序启动 (高DPI物理分辨率修复版)");
    
    // 解析命令行
    ParseCommandLine();
    
    // 注册窗口类
    if (!RegisterWindowClass(hInstance)) {
        WriteLog(LOG_ERROR, L"注册窗口类失败");
        MessageBoxW(NULL, L"注册窗口类失败", L"错误", MB_OK | MB_ICONERROR);
        GdiplusShutdown(g_gdiplusToken);
        return 1;
    }
    
    // 创建主窗口
    DWORD dwStyle = WS_OVERLAPPEDWINDOW;
    if (g_bInBackground) {
        dwStyle = WS_OVERLAPPEDWINDOW & ~WS_VISIBLE;
    }
    
    g_hMainWnd = CreateWindowW(L"ScreenshotCaptureClass", L"截图捕获程序",
        dwStyle,
        CW_USEDEFAULT, 0, 800, 600,
        NULL, NULL, hInstance, NULL);
    
    if (!g_hMainWnd) {
        WriteLog(LOG_ERROR, L"创建主窗口失败");
        MessageBoxW(NULL, L"创建主窗口失败", L"错误", MB_OK | MB_ICONERROR);
        GdiplusShutdown(g_gdiplusToken);
        return 1;
    }
    
    // 显示窗口
    if (!g_bInBackground) {
        ShowWindow(g_hMainWnd, nCmdShow);
        UpdateWindow(g_hMainWnd);
    } else {
        ShowWindow(g_hMainWnd, SW_HIDE);
    }
    
    // 首先尝试注册系统热键
    if (!RegisterHotKeyAlternate()) {
        // 如果热键注册失败，使用全局键盘钩子
        WriteLog(LOG_INFO, L"使用全局键盘钩子监听热键 (Ctrl+PrtSc 和 Ctrl+F1)");
        g_hKeyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);
        if (!g_hKeyboardHook) {
            WriteLog(LOG_WARNING, L"设置键盘钩子失败，错误代码: " + std::to_wstring(GetLastError()));
            MessageBoxW(g_hMainWnd, L"无法注册热键或键盘钩子，程序将无法响应Ctrl+PrtSc和Ctrl+F1", 
                       L"警告", MB_OK | MB_ICONWARNING);
        } else {
            WriteLog(LOG_INFO, L"键盘钩子设置成功");
        }
    }
    
    WriteLog(LOG_INFO, L"程序已就绪，支持热键: Ctrl+PrtSc 和 Ctrl+F1");
    WriteLog(LOG_INFO, L"截图格式: JPEG (质量90%)，保存路径: C:\\screenshots\\");
    WriteLog(LOG_INFO, L"已修复高DPI屏幕截图问题，使用StretchBlt拉伸到物理分辨率");
    
    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // 清理GDI+
    GdiplusShutdown(g_gdiplusToken);
    
    return (int)msg.wParam;
}