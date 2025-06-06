/*
 * Copyright (C) 2006-2023 Apple Inc.  All rights reserved.
 * Copyright (C) 2009, 2011 Brent Fulgham.  All rights reserved.
 * Copyright (C) 2009, 2010, 2011 Appcelerator, Inc. All rights reserved.
 * Copyright (C) 2013 Alex Christensen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "stdafx.h"
#include "Common.h"

#include "DialogHelper.h"
#include "MiniBrowserLibResource.h"
#include "MiniBrowserReplace.h"
#include <dbghelp.h>
#include <shlobj.h>
#include <wtf/StdLibExtras.h>

// Global Variables:
HINSTANCE hInst;

// Support moving the transparent window
POINT s_windowPosition = { 100, 100 };
SIZE s_windowSize = { 500, 200 };

namespace WebCore {
float deviceScaleFactorForWindow(HWND);
}

void computeFullDesktopFrame()
{
    RECT desktop;
    if (!::SystemParametersInfo(SPI_GETWORKAREA, 0, static_cast<void*>(&desktop), 0))
        return;

    float scaleFactor = WebCore::deviceScaleFactorForWindow(nullptr);

    s_windowPosition.x = 0;
    s_windowPosition.y = 0;
    s_windowSize.cx = scaleFactor * (desktop.right - desktop.left);
    s_windowSize.cy = scaleFactor * (desktop.bottom - desktop.top);
}

bool getAppDataFolder(_bstr_t& directory)
{
    wchar_t appDataDirectory[MAX_PATH];
    if (FAILED(SHGetFolderPathW(0, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, 0, 0, appDataDirectory)))
        return false;

    wchar_t executablePath[MAX_PATH];
    if (!::GetModuleFileNameW(0, executablePath, MAX_PATH))
        return false;

    ::PathRemoveExtensionW(executablePath);

    directory = _bstr_t(appDataDirectory) + L"\\" + ::PathFindFileNameW(executablePath);

    return true;
}

bool getKnownFolderPath(REFKNOWNFOLDERID id, std::wstring& knownFolderPath)
{
    PWSTR path = nullptr;

    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, 0, &path)))
        return false;

    knownFolderPath = std::wstring(path);
    CoTaskMemFree(path);
    return true;
}

void createCrashReport(EXCEPTION_POINTERS* exceptionPointers)
{
    _bstr_t directory;

    if (!getAppDataFolder(directory))
        return;

    if (::SHCreateDirectoryEx(0, directory, 0) != ERROR_SUCCESS
        && ::GetLastError() != ERROR_FILE_EXISTS
        && ::GetLastError() != ERROR_ALREADY_EXISTS)
        return;

    std::wstring fileName = std::wstring(static_cast<const wchar_t*>(directory)) + L"\\CrashReport.dmp";
    HANDLE miniDumpFile = ::CreateFile(fileName.c_str(), GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

    if (miniDumpFile && miniDumpFile != INVALID_HANDLE_VALUE) {

        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = ::GetCurrentThreadId();
        mdei.ExceptionPointers  = exceptionPointers;
        mdei.ClientPointers = 0;

#ifdef _DEBUG
        MINIDUMP_TYPE dumpType = MiniDumpWithFullMemory;
#else
        MINIDUMP_TYPE dumpType = MiniDumpNormal;
#endif

        ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), miniDumpFile, dumpType, &mdei, 0, 0);
        ::CloseHandle(miniDumpFile);
        processCrashReport(fileName.c_str());
    }
}

bool askProxySettings(HWND hwnd, ProxySettings& settings)
{
    class ProxyDialog : public Dialog {
    public:
        ProxyDialog(ProxySettings& settings)
            : settings { settings }
        {
        }

    protected:
        ProxySettings& settings;

        void setup() final
        {
            auto command = commandForProxyChoice();
            proxyChoice().set(command);
            setText(IDC_PROXY_URL, settings.url);
            setText(IDC_PROXY_EXCLUDE, settings.excludeHosts);
        }

        void ok() final
        {
            settings.url = getText(IDC_PROXY_URL);
            settings.excludeHosts = getText(IDC_PROXY_EXCLUDE);
            updateProxyChoice(proxyChoice().get());
        }

        bool validate() final
        {
            bool valid = true;

            if (proxyChoice().get() == IDC_PROXY_CUSTOM) {
                setEnabled(IDC_PROXY_URL, true);
                setEnabled(IDC_PROXY_EXCLUDE, true);

                if (!getTextLength(IDC_PROXY_URL))
                    valid = false;
            } else {
                setEnabled(IDC_PROXY_URL, false);
                setEnabled(IDC_PROXY_EXCLUDE, false);
            }

            return valid;
        }

        RadioGroup proxyChoice()
        {
            return radioGroup(IDC_PROXY_DEFAULT, IDC_PROXY_DISABLE);
        }

        int commandForProxyChoice()
        {
            if (!settings.enable)
                return IDC_PROXY_DISABLE;
            if (settings.custom)
                return IDC_PROXY_CUSTOM;
            return IDC_PROXY_DEFAULT;
        }

        void updateProxyChoice(int command)
        {
            switch (command) {
            case IDC_PROXY_DEFAULT:
                settings.enable = true;
                settings.custom = false;
                break;
            case IDC_PROXY_CUSTOM:
                settings.enable = true;
                settings.custom = true;
                break;
            case IDC_PROXY_DISABLE:
                settings.enable = false;
                settings.custom = false;
                break;
            default:
                break;
            }
        }
    };

    ProxyDialog dialog { settings };
    return dialog.run(hInst, hwnd, IDD_PROXY);
}

std::optional<Credential> askCredential(HWND hwnd, const std::wstring& text)
{
    struct AuthDialog : public Dialog {
        std::wstring text;
        Credential credential;

    protected:
        void setup()
        {
            setText(IDC_AUTH_TEXT, text);
        }

        void ok() final
        {
            credential.username = getText(IDC_AUTH_USER);
            credential.password = getText(IDC_AUTH_PASSWORD);
        }
    };

    AuthDialog dialog;
    dialog.text = text;

    if (dialog.run(hInst, hwnd, IDD_AUTH))
        return dialog.credential;
    return std::nullopt;
}

bool askServerTrustEvaluation(HWND hwnd, const std::wstring& text)
{
    class ServerTrustEvaluationDialog : public Dialog {
    public:
        ServerTrustEvaluationDialog(const std::wstring& text)
            : m_text { text }
        {
            SendMessage(GetDlgItem(this->hDlg(), IDC_SERVER_TRUST_TEXT), WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), TRUE);
        }

    protected:
        std::wstring m_text;

        void setup()
        {
            setText(IDC_SERVER_TRUST_TEXT, m_text);
        }

        void ok() final
        {

        }
    };

    ServerTrustEvaluationDialog dialog { text };
    return dialog.run(hInst, hwnd, IDD_SERVER_TRUST);
}

CommandLineOptions parseCommandLine()
{
    CommandLineOptions options;

    int argc = 0;
    WCHAR** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        if (!wcsicmp(argv[i], L"--transparent"))
            options.usesLayeredWebView = true;
        else if (!wcsicmp(argv[i], L"--desktop"))
            options.useFullDesktop = true;
        else if (!options.requestedURL)
            options.requestedURL = argv[i];
    }

    return options;
}

std::wstring replaceString(std::wstring src, const std::wstring& oldValue, const std::wstring& newValue)
{
    if (src.empty() || oldValue.empty())
        return src;

    size_t pos = 0;
    while ((pos = src.find(oldValue, pos)) != src.npos) {
        src.replace(pos, oldValue.length(), newValue);
        pos += newValue.length();
    }

    return src;
}
