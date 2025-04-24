#include <iostream>
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <filesystem>
#include <fstream>
#include <commctrl.h>
#include <thread>
#include <atomic>
#include "resource.h"

// Link with the Common Controls library
#pragma comment(lib, "comctl32.lib")

namespace fs = std::filesystem;

// Global UI variables
HWND g_hwndMain = NULL;
HWND g_hwndProgressBar = NULL;
HWND g_hwndStatus = NULL;
HWND g_hwndEditPath = NULL;
HWND g_hwndCheckStartZalo = NULL;
std::atomic<bool> g_isRunning = false;
HINSTANCE g_hInstance = NULL;

// Process steps for progress tracking
enum ProcessStep {
    STEP_INIT = 0,
    STEP_CLOSE_ZALO = 10,
    STEP_CHECK_DIRECTORIES = 20,
    STEP_COPY_FILES = 30,
    STEP_REMOVE_OLD_DIR = 80,
    STEP_CREATE_LINK = 90,
    STEP_COMPLETE = 100
};

// Check for administrator privileges
bool IsRunAsAdmin() {
    BOOL fIsRunAsAdmin = FALSE;
    DWORD dwError = ERROR_SUCCESS;
    PSID pAdministratorsGroup = NULL;

    // Initialize SID for the Administrators group
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid(
        &NtAuthority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &pAdministratorsGroup)) {
        dwError = GetLastError();
        goto Cleanup;
    }

    // Check if the current process is in the Administrators group
    if (!CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin)) {
        dwError = GetLastError();
        goto Cleanup;
    }

Cleanup:
    if (pAdministratorsGroup) {
        FreeSid(pAdministratorsGroup);
        pAdministratorsGroup = NULL;
    }

    return fIsRunAsAdmin;
}

// Get user's AppData Local path
std::wstring GetAppDataLocalPath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        return std::wstring(path);
    }
    // Fallback if SHGetFolderPath fails
    wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
    if (localAppData) {
        return std::wstring(localAppData);
    }
    // Last resort fallback
    return L"C:\\Users\\Public\\AppData\\Local";
}

// Get Zalo data path
std::wstring GetZaloDataPath() {
    return GetAppDataLocalPath() + L"\\ZaloPC";
}

// Update progress bar and status text
void UpdateProgress(ProcessStep step, const std::wstring& message) {
    if (g_hwndMain) {
        SendMessage(g_hwndProgressBar, PBM_SETPOS, static_cast<WPARAM>(step), 0);
        SendMessage(g_hwndMain, WM_UPDATE_STATUS, 0, (LPARAM)message.c_str());
    }
}

// Show folder selection dialog
std::wstring SelectFolderDialog(HWND hwndOwner) {
    wchar_t path[MAX_PATH];
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = hwndOwner;
    bi.lpszTitle = L"Select folder to store Zalo data";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != 0) {
        SHGetPathFromIDListW(pidl, path);
        IMalloc* pMalloc = NULL;
        if (SUCCEEDED(SHGetMalloc(&pMalloc))) {
            pMalloc->Free(pidl);
            pMalloc->Release();
        }
        return std::wstring(path);
    }
    return std::wstring();
}

// Close Zalo process if running
bool CloseZaloProcess() {
    UpdateProgress(STEP_CLOSE_ZALO, L"Closing Zalo process if running...");
    
    // Use taskkill to terminate Zalo
    int result = system("taskkill /f /im Zalo.exe >nul 2>&1");
    
    // Wait to ensure process has ended
    Sleep(2000);
    
    return result == 0 || result == 128; // 128 usually means process not found
}

// Move Zalo data to the new location
bool MoveZaloData(const std::wstring& targetDir) {
    std::wstring zaloDataPath = GetZaloDataPath();
    std::wstring newZaloDataPath = targetDir + L"\\ZaloPC";
    
    UpdateProgress(STEP_CHECK_DIRECTORIES, L"Checking directories...");
    
    try {
        // Check if source directory exists
        if (!fs::exists(zaloDataPath)) {
            std::wstring errorMsg = L"Zalo data directory not found at: " + zaloDataPath;
            UpdateProgress(STEP_CHECK_DIRECTORIES, errorMsg);
            return false;
        }
        
        // Check if destination directory already exists
        if (fs::exists(newZaloDataPath)) {
            int result = MessageBoxW(g_hwndMain, 
                L"Destination folder already exists. Do you want to delete it?", 
                L"Confirm", 
                MB_YESNO | MB_ICONQUESTION);
                
            if (result != IDYES) {
                UpdateProgress(STEP_CHECK_DIRECTORIES, L"Operation canceled.");
                return false;
            }
            
            UpdateProgress(STEP_CHECK_DIRECTORIES, L"Deleting old destination folder...");
            fs::remove_all(newZaloDataPath);
        }
        
        // Check if source is a symbolic link
        DWORD attributes = GetFileAttributesW(zaloDataPath.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            UpdateProgress(STEP_CHECK_DIRECTORIES, L"Detected existing symbolic link...");
            
            // Copy data from the symbolic link target
            try {
                fs::create_directories(newZaloDataPath);
                
                // Count files for progress tracking
                size_t totalItems = 0;
                size_t processedItems = 0;
                for (const auto& entry : fs::recursive_directory_iterator(zaloDataPath)) {
                    totalItems++;
                }
                
                // Copy all files from source (symbolic link target) to destination
                for (const auto& entry : fs::directory_iterator(zaloDataPath)) {
                    const auto& path = entry.path();
                    const auto& targetPath = fs::path(newZaloDataPath) / path.filename();
                    
                    std::wstring statusMsg = L"Copying: " + path.filename().wstring();
                    ProcessStep currentStep = static_cast<ProcessStep>(STEP_COPY_FILES + static_cast<int>((processedItems * 50) / totalItems));
                    UpdateProgress(currentStep, statusMsg);
                    
                    if (fs::is_directory(path)) {
                        fs::copy(path, targetPath, fs::copy_options::recursive);
                    } else {
                        fs::copy_file(path, targetPath, fs::copy_options::overwrite_existing);
                    }
                    
                    processedItems++;
                }
            } catch (const std::exception& e) {
                std::wstring errorMsg = L"Error copying from symbolic link: ";
                errorMsg += std::wstring(e.what(), e.what() + strlen(e.what()));
                UpdateProgress(STEP_COPY_FILES, errorMsg);
                return false;
            }
            
            // Remove old symbolic link
            UpdateProgress(STEP_REMOVE_OLD_DIR, L"Removing old symbolic link...");
            if (!fs::remove(zaloDataPath)) {
                UpdateProgress(STEP_REMOVE_OLD_DIR, L"Cannot remove old symbolic link!");
                return false;
            }
        } else {
            // Regular directory - copy contents
            try {
                fs::create_directories(fs::path(newZaloDataPath).parent_path());
                
                // Count files for progress tracking
                size_t totalItems = 0;
                size_t processedItems = 0;
                for (const auto& entry : fs::recursive_directory_iterator(zaloDataPath)) {
                    totalItems++;
                }
                
                UpdateProgress(STEP_COPY_FILES, L"Copying Zalo data to new location...");
                
                // Copy files one by one with progress tracking
                fs::create_directories(newZaloDataPath);
                for (const auto& entry : fs::recursive_directory_iterator(zaloDataPath)) {
                    const fs::path& sourcePath = entry.path();
                    fs::path relativePath = fs::relative(sourcePath, zaloDataPath);
                    fs::path targetPath = fs::path(newZaloDataPath) / relativePath;
                    
                    std::wstring statusMsg = L"Copying: " + relativePath.wstring();
                    ProcessStep currentStep = static_cast<ProcessStep>(STEP_COPY_FILES + static_cast<int>((processedItems * 50) / totalItems));
                    UpdateProgress(currentStep, statusMsg);
                    
                    if (fs::is_directory(sourcePath)) {
                        fs::create_directories(targetPath);
                    } else {
                        fs::create_directories(targetPath.parent_path());
                        fs::copy_file(sourcePath, targetPath, fs::copy_options::overwrite_existing);
                    }
                    
                    processedItems++;
                }
                
                UpdateProgress(STEP_REMOVE_OLD_DIR, L"Removing old data directory...");
                fs::remove_all(zaloDataPath);
            } catch (const std::exception& e) {
                std::wstring errorMsg = L"Error moving data: " + std::wstring(e.what(), e.what() + strlen(e.what()));
                UpdateProgress(STEP_COPY_FILES, errorMsg);
                return false;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::wstring errorMsg = L"Error moving data: " + std::wstring(e.what(), e.what() + strlen(e.what()));
        UpdateProgress(STEP_COPY_FILES, errorMsg);
        return false;
    }
}

// Create junction link - simplified to use only the most reliable method
bool CreateJunctionLink(const std::wstring& targetDir) {
    std::wstring zaloDataPath = GetZaloDataPath();
    std::wstring newZaloDataPath = targetDir + L"\\ZaloPC";
    
    UpdateProgress(STEP_CREATE_LINK, L"Creating junction link...");
    
    // Use cmd to create junction link (most reliable method)
    std::wstring command = L"cmd.exe /c mklink /j \"" + zaloDataPath + L"\" \"" + newZaloDataPath + L"\"";
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(SHELLEXECUTEINFOW);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";  // Run as admin
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = (L"/c mklink /j \"" + zaloDataPath + L"\" \"" + newZaloDataPath + L"\"").c_str();
    sei.nShow = SW_HIDE;
    
    if (!ShellExecuteExW(&sei)) {
        UpdateProgress(STEP_CREATE_LINK, L"Failed to create junction link. Manual creation required.");
        
        // Show instructions for manual link creation with full path
        std::wstring manualLinkCmd = L"mklink /j \"" + zaloDataPath + L"\" \"" + newZaloDataPath + L"\"";
        std::wstring message = L"Failed to create junction link automatically.\n\n"
                              L"Please follow these steps:\n"
                              L"1. Open Command Prompt as Administrator\n"
                              L"2. Run this command:\n"
                              L"   " + manualLinkCmd + L"\n\n"
                              L"Have you completed these steps?";
        
        int result = MessageBoxW(g_hwndMain, message.c_str(), L"Manual Junction Link Creation", MB_YESNO | MB_ICONQUESTION);
        if (result == IDYES) {
            // Verify if link was created
            if (fs::exists(zaloDataPath)) {
                UpdateProgress(STEP_CREATE_LINK, L"Junction link created successfully!");
                return true;
            }
        }
        
        return false;
    }
    
    // Wait for process to complete
    WaitForSingleObject(sei.hProcess, INFINITE);
    
    // Get exit code
    DWORD exitCode;
    GetExitCodeProcess(sei.hProcess, &exitCode);
    
    // Close handle
    CloseHandle(sei.hProcess);
    
    if (exitCode == 0 || fs::exists(zaloDataPath)) {
        UpdateProgress(STEP_CREATE_LINK, L"Junction link created successfully.");
        return true;
    }
    
    return false;
}

// Start Zalo application
void StartZalo() {
    UpdateProgress(STEP_COMPLETE, L"Starting Zalo...");
    
    // Find Zalo executable with a more dynamic approach
    std::wstring programFilesPath = GetAppDataLocalPath() + L"\\Programs\\Zalo\\Zalo.exe";
    
    // Check if the executable exists
    if (!fs::exists(programFilesPath)) {
        UpdateProgress(STEP_COMPLETE, L"Zalo executable not found. Please start Zalo manually.");
        return;
    }
    
    // Launch Zalo
    ShellExecuteW(NULL, L"open", programFilesPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

// Write log to file
void WriteLog(const std::string& message, const std::wstring& logPath) {
    std::wstring logFile = logPath + L"\\zalo_data_mover.log";
    std::ofstream log(logFile, std::ios_base::app);
    if (log.is_open()) {
        time_t now = time(0);
        char timeBuffer[26];
        ctime_s(timeBuffer, sizeof(timeBuffer), &now);
        timeBuffer[strlen(timeBuffer) - 1] = '\0';
        log << "[" << timeBuffer << "] " << message << std::endl;
        log.close();
    }
}

// Run Zalo data moving process in a separate thread
void RunZaloDataMoverProcess(const std::wstring& targetDir) {
    g_isRunning = true;
    
    std::wstring zaloDataPath = GetZaloDataPath();
    UpdateProgress(STEP_INIT, L"Starting Zalo data migration process from: " + zaloDataPath);
    
    // 1. Close Zalo if running
    if (!CloseZaloProcess()) {
        UpdateProgress(STEP_CLOSE_ZALO, L"Warning: Could not close Zalo or it's not running.");
    }
    
    // 2. Move data
    if (!MoveZaloData(targetDir)) {
        UpdateProgress(STEP_COPY_FILES, L"Failed to move Zalo data!");
        WriteLog("Data migration failed", targetDir);
        g_isRunning = false;
        SendMessage(g_hwndMain, WM_OPERATION_DONE, 0, 0);
        return;
    }
    
    // 3. Create junction link
    if (!CreateJunctionLink(targetDir)) {
        UpdateProgress(STEP_CREATE_LINK, L"Failed to create junction link!");
        WriteLog("Junction link creation failed", targetDir);
        g_isRunning = false;
        SendMessage(g_hwndMain, WM_OPERATION_DONE, 0, 0);
        return;
    }
    
    // 4. Complete
    UpdateProgress(STEP_COMPLETE, L"Data successfully moved from:\n" + zaloDataPath + L"\nto:\n" + targetDir + L"\\ZaloPC");
    WriteLog("Data migration successful", targetDir);
    
    // 5. Start Zalo if selected
    BOOL isChecked = (BOOL)SendMessage(g_hwndCheckStartZalo, BM_GETCHECK, 0, 0);
    if (isChecked == BST_CHECKED) {
        StartZalo();
    }
    
    g_isRunning = false;
    SendMessage(g_hwndMain, WM_OPERATION_DONE, 1, 0);
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // Initialize Common Controls
            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC = ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES;
            InitCommonControlsEx(&icex);
            
            // Create font for controls
            HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            
            // Create path label
            HWND hwndPathLabel = CreateWindowW(
                L"STATIC", L"Destination folder:",
                WS_VISIBLE | WS_CHILD,
                20, 20, 100, 20,
                hwnd, NULL, g_hInstance, NULL);
            SendMessage(hwndPathLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Create path textbox
            g_hwndEditPath = CreateWindowW(
                L"EDIT", L"",
                WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
                130, 20, 400, 25,
                hwnd, (HMENU)IDC_TARGET_PATH, g_hInstance, NULL);
            SendMessage(g_hwndEditPath, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Create Browse button
            HWND hwndBtnBrowse = CreateWindowW(
                L"BUTTON", L"Browse...",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                540, 20, 80, 25,
                hwnd, (HMENU)IDC_BTN_BROWSE, g_hInstance, NULL);
            SendMessage(hwndBtnBrowse, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Create auto-start Zalo checkbox
            g_hwndCheckStartZalo = CreateWindowW(
                L"BUTTON", L"Auto-restart Zalo after completion",
                WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                20, 60, 350, 25,
                hwnd, (HMENU)IDC_CHECKBOX_START_ZALO, g_hInstance, NULL);
            SendMessage(g_hwndCheckStartZalo, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(g_hwndCheckStartZalo, BM_SETCHECK, BST_CHECKED, 0);
            
            // Create Start button
            HWND hwndBtnStart = CreateWindowW(
                L"BUTTON", L"Start Migration",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED,
                20, 100, 600, 40,
                hwnd, (HMENU)IDC_BTN_START, g_hInstance, NULL);
            SendMessage(hwndBtnStart, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            // Create progress bar
            g_hwndProgressBar = CreateWindowExW(
                0, PROGRESS_CLASSW, NULL,
                WS_VISIBLE | WS_CHILD,
                20, 160, 600, 30,
                hwnd, (HMENU)IDC_PROGRESS_BAR, g_hInstance, NULL);
            SendMessage(g_hwndProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessage(g_hwndProgressBar, PBM_SETPOS, 0, 0);
            
            // Create status text
            g_hwndStatus = CreateWindowW(
                L"STATIC", L"Please select a destination folder to begin...",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                20, 200, 600, 60,
                hwnd, (HMENU)IDC_STATUS_TEXT, g_hInstance, NULL);
            SendMessage(g_hwndStatus, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            break;
        }

        case WM_COMMAND: {
            WORD ctrlId = LOWORD(wParam);
            
            switch (ctrlId) {
                case IDC_BTN_BROWSE: {
                    if (g_isRunning) break;
                    
                    std::wstring selectedPath = SelectFolderDialog(hwnd);
                    if (!selectedPath.empty()) {
                        SetWindowTextW(g_hwndEditPath, selectedPath.c_str());
                        
                        // Enable Start button when path is selected
                        EnableWindow(GetDlgItem(hwnd, IDC_BTN_START), TRUE);
                    }
                    break;
                }
                
                case IDC_BTN_START: {
                    if (g_isRunning) break;
                    
                    wchar_t targetDir[MAX_PATH];
                    GetWindowTextW(g_hwndEditPath, targetDir, MAX_PATH);
                    
                    if (wcslen(targetDir) == 0) {
                        MessageBoxW(hwnd, L"Please select a destination folder first!", L"Error", MB_OK | MB_ICONERROR);
                        break;
                    }
                    
                    // Disable controls during operation
                    EnableWindow(GetDlgItem(hwnd, IDC_BTN_START), FALSE);
                    EnableWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE), FALSE);
                    EnableWindow(g_hwndEditPath, FALSE);
                    EnableWindow(g_hwndCheckStartZalo, FALSE);
                    
                    // Start worker thread
                    std::thread workerThread(RunZaloDataMoverProcess, std::wstring(targetDir));
                    workerThread.detach();
                    break;
                }
            }
            break;
        }
        
        case WM_UPDATE_STATUS: {
            // Update status text
            SetWindowTextW(g_hwndStatus, (LPCWSTR)lParam);
            break;
        }
        
        case WM_OPERATION_DONE: {
            // Re-enable controls when done
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_START), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE), TRUE);
            EnableWindow(g_hwndEditPath, TRUE);
            EnableWindow(g_hwndCheckStartZalo, TRUE);
            
            // Show completion message if successful
            if (wParam == 1) {
                wchar_t targetDir[MAX_PATH];
                GetWindowTextW(g_hwndEditPath, targetDir, MAX_PATH);
                std::wstring sourcePath = GetZaloDataPath();
                std::wstring message = L"Zalo data has been successfully moved from:\n" + 
                                      sourcePath + L"\n\nto:\n" + 
                                      std::wstring(targetDir) + L"\\ZaloPC";
                
                MessageBoxW(hwnd, message.c_str(), L"Complete", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }
        
        case WM_CLOSE:
            if (g_isRunning) {
                if (MessageBoxW(hwnd, L"Operation in progress. Are you sure you want to exit?", L"Confirm", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                    break;
                }
            }
            DestroyWindow(hwnd);
            break;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
            
        default:
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    
    return 0;
}

// Main entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;
    
    
    // Register window class
    const wchar_t CLASS_NAME[] = L"ZaloDataMoverWindowClass";
    
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(IDI_APPLICATION));
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(IDC_ARROW));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    
    RegisterClassW(&wc);
    
    // Create main window
    g_hwndMain = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Zalo Data Migration Tool",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 650, 300,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    
    if (g_hwndMain == NULL) {
        MessageBoxW(NULL, L"Failed to create window!", L"Error", MB_OK | MB_ICONERROR);
        return 0;
    }
    
    // Show window
    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);
    
    // Message loop
    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}

// Set application subsystem to Windows
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
