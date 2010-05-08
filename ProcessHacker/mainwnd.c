/*
 * Process Hacker - 
 *   main window
 * 
 * Copyright (C) 2009-2010 wj32
 * 
 * This file is part of Process Hacker.
 * 
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#define MAINWND_PRIVATE
#include <phapp.h>
#include <treelist.h>
#include <settings.h>
#include <wtsapi32.h>

typedef BOOL (WINAPI *_FileIconInit)(
    __in BOOL RestoreCache
    );

typedef BOOL (WINAPI *_WTSRegisterSessionNotification)(
    __in HWND hWnd,
    __in DWORD dwFlags
    );

VOID PhMainWndOnCreate();
VOID PhMainWndOnLayout(HDWP *deferHandle);
VOID PhMainWndTabControlOnLayout(HDWP *deferHandle);
VOID PhMainWndTabControlOnNotify(
    __in LPNMHDR Header
    );
VOID PhMainWndTabControlOnSelectionChanged();
VOID PhMainWndProcessListViewOnNotify(
    __in LPNMHDR Header
    );
VOID PhMainWndServiceListViewOnNotify(
    __in LPNMHDR Header
    );

VOID PhReloadSysParameters();

VOID PhpSaveWindowState();

VOID PhpSaveAllSettings();

VOID PhpPrepareForEarlyShutdown();

VOID PhpCancelEarlyShutdown();

VOID PhpRefreshUsersMenu();

PPH_PROCESS_ITEM PhpGetSelectedProcess();

VOID PhpGetSelectedProcesses(
    __out PPH_PROCESS_ITEM **Processes,
    __out PULONG NumberOfProcesses
    );

PPH_SERVICE_ITEM PhpGetSelectedService();

VOID PhpGetSelectedServices(
    __out PPH_SERVICE_ITEM **Services,
    __out PULONG NumberOfServices
    );

VOID PhpShowProcessProperties(
    __in PPH_PROCESS_ITEM ProcessItem
    );

VOID PhMainWndOnProcessAdded(
    __in __assumeRefs(1) PPH_PROCESS_ITEM ProcessItem
    );

VOID PhMainWndOnProcessModified(
    __in PPH_PROCESS_ITEM ProcessItem
    );

VOID PhMainWndOnProcessRemoved(
    __in PPH_PROCESS_ITEM ProcessItem
    );

VOID PhMainWndOnServiceAdded(
    __in ULONG RunId,
    __in PPH_SERVICE_ITEM ServiceItem
    );

VOID PhMainWndOnServiceModified(
    __in PPH_SERVICE_MODIFIED_DATA ServiceModifiedData
    );

VOID PhMainWndOnServiceRemoved(
    __in PPH_SERVICE_ITEM ServiceItem
    );

VOID PhMainWndOnServicesUpdated();

HWND PhMainWndHandle;
BOOLEAN PhMainWndExiting = FALSE;

static HWND TabControlHandle;
static INT ProcessesTabIndex;
static INT ServicesTabIndex;
static INT NetworkTabIndex;
static HWND ProcessTreeListHandle;
static HWND ServiceListViewHandle;
static HWND NetworkListViewHandle;

static PH_PROVIDER_REGISTRATION ProcessProviderRegistration;
static PH_CALLBACK_REGISTRATION ProcessAddedRegistration;
static PH_CALLBACK_REGISTRATION ProcessModifiedRegistration;
static PH_CALLBACK_REGISTRATION ProcessRemovedRegistration;

static PH_PROVIDER_REGISTRATION ServiceProviderRegistration;
static PH_CALLBACK_REGISTRATION ServiceAddedRegistration;
static PH_CALLBACK_REGISTRATION ServiceModifiedRegistration;
static PH_CALLBACK_REGISTRATION ServiceRemovedRegistration; 
static PH_CALLBACK_REGISTRATION ServicesUpdatedRegistration;

static BOOLEAN SelectedRunAsAdmin;
static HWND SelectedProcessWindowHandle;
static BOOLEAN SelectedProcessVirtualizationEnabled;
static ULONG SelectedUserSessionId;

BOOLEAN PhMainWndInitialization(
    __in INT ShowCommand
    )
{
    PH_RECTANGLE windowRectangle;

    // Enable some privileges.
    {
        HANDLE tokenHandle;

        if (NT_SUCCESS(PhOpenProcessToken(
            &tokenHandle,
            TOKEN_ADJUST_PRIVILEGES,
            NtCurrentProcess()
            )))
        {
            PhSetTokenPrivilege(tokenHandle, L"SeDebugPrivilege", NULL, SE_PRIVILEGE_ENABLED);
            PhSetTokenPrivilege(tokenHandle, L"SeIncreaseBasePriorityPrivilege", NULL, SE_PRIVILEGE_ENABLED);
            PhSetTokenPrivilege(tokenHandle, L"SeLoadDriverPrivilege", NULL, SE_PRIVILEGE_ENABLED);
            PhSetTokenPrivilege(tokenHandle, L"SeRestorePrivilege", NULL, SE_PRIVILEGE_ENABLED);
            PhSetTokenPrivilege(tokenHandle, L"SeShutdownPrivilege", NULL, SE_PRIVILEGE_ENABLED);
            PhSetTokenPrivilege(tokenHandle, L"SeTakeOwnershipPrivilege", NULL, SE_PRIVILEGE_ENABLED);
            NtClose(tokenHandle);
        }
    }

    // Initialize the system image lists.
    {
        HMODULE shell32;
        _FileIconInit fileIconInit;

        shell32 = LoadLibrary(L"shell32.dll");

        if (shell32)
        {
            fileIconInit = (_FileIconInit)GetProcAddress(shell32, (PSTR)660);

            if (fileIconInit)
                fileIconInit(FALSE);
        }
    }

    // Initialize dbghelp.
    {
        PPH_STRING dbghelpPath;
        HMODULE dbghelpModule;

        // Try to set up the path automatically if this is the first run. 
        {
            if (PhGetIntegerSetting(L"FirstRun"))
            {
                PPH_STRING autoDbghelpPath;

                autoDbghelpPath = PHA_DEREFERENCE(PhGetKnownLocation(
                    CSIDL_PROGRAM_FILES,
#ifdef _M_IX86
                    L"\\Debugging Tools for Windows (x86)\\dbghelp.dll"
#else
                    L"\\Debugging Tools for Windows (x64)\\dbghelp.dll"
#endif
                    ));

                if (autoDbghelpPath)
                {
                    if (PhFileExists(autoDbghelpPath->Buffer))
                    {
                        PhSetStringSetting2(L"DbgHelpPath", &autoDbghelpPath->sr);
                    }
                }
            }
        }

        dbghelpPath = PhGetStringSetting(L"DbgHelpPath");

        if (dbghelpModule = LoadLibrary(dbghelpPath->Buffer))
        {
            PPH_STRING fullDbghelpPath;
            ULONG indexOfFileName;
            PPH_STRING dbghelpFolder;
            PPH_STRING symsrvPath;

            fullDbghelpPath = PhGetApplicationModuleFileName(dbghelpModule, &indexOfFileName);

            if (fullDbghelpPath)
            {
                if (indexOfFileName != -1)
                {
                    dbghelpFolder = PhSubstring(fullDbghelpPath, 0, indexOfFileName);
                    symsrvPath = PhConcatStrings2(dbghelpFolder->Buffer, L"\\symsrv.dll");
                    LoadLibrary(symsrvPath->Buffer);

                    PhDereferenceObject(symsrvPath);
                    PhDereferenceObject(dbghelpFolder);
                }

                PhDereferenceObject(fullDbghelpPath);
            }
        }
        else
        {
            LoadLibrary(L"dbghelp.dll");
        }

        PhDereferenceObject(dbghelpPath);

        PhSymbolProviderDynamicImport();
    }

    PhSetIntegerSetting(L"FirstRun", FALSE);

    // Initialize the providers.
    PhInitializeProviderThread(&PhPrimaryProviderThread, 1000);
    PhInitializeProviderThread(&PhSecondaryProviderThread, 1000);

    PhRegisterProvider(&PhPrimaryProviderThread, PhProcessProviderUpdate, NULL, &ProcessProviderRegistration);
    PhSetProviderEnabled(&ProcessProviderRegistration, TRUE);
    PhRegisterProvider(&PhPrimaryProviderThread, PhServiceProviderUpdate, NULL, &ServiceProviderRegistration);
    PhSetProviderEnabled(&ServiceProviderRegistration, TRUE);

    windowRectangle.Position = PhGetIntegerPairSetting(L"MainWindowPosition");
    windowRectangle.Size = PhGetIntegerPairSetting(L"MainWindowSize");

    PhMainWndHandle = CreateWindow(
        PhWindowClassName,
        PhApplicationName,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        windowRectangle.Left,
        windowRectangle.Top,
        windowRectangle.Width,
        windowRectangle.Height,
        NULL,
        NULL,
        PhInstanceHandle,
        NULL
        );

    if (!PhMainWndHandle)
        return FALSE;

    // Choose a more appropriate rectangle for the window.
    PhAdjustRectangleToWorkingArea(
        PhMainWndHandle,
        &windowRectangle
        );
    MoveWindow(PhMainWndHandle, windowRectangle.Left, windowRectangle.Top,
        windowRectangle.Width, windowRectangle.Height, FALSE);

    PhInitializeFont(PhMainWndHandle);

    // Allow WM_PH_ACTIVATE to pass through UIPI.
    if (WINDOWS_HAS_UAC)
        ChangeWindowMessageFilter_I(WM_PH_ACTIVATE, MSGFLT_ADD);

    // Create the window title.
    {
        PH_STRING_BUILDER stringBuilder;

        PhInitializeStringBuilder(&stringBuilder, 50);
        PhStringBuilderAppend2(&stringBuilder, L"Process Hacker");

        if (PhCurrentUserName)
        {
            PhStringBuilderAppend2(&stringBuilder, L" [");
            PhStringBuilderAppend(&stringBuilder, PhCurrentUserName);
            PhStringBuilderAppendChar(&stringBuilder, ']');
        }

        if (WINDOWS_HAS_UAC && PhElevationType == TokenElevationTypeFull)
            PhStringBuilderAppend2(&stringBuilder, L" (Administrator)");

        SetWindowText(PhMainWndHandle, stringBuilder.String->Buffer);

        PhDeleteStringBuilder(&stringBuilder);
    }

    PhReloadSysParameters();

    // Initialize child controls.
    PhMainWndOnCreate();

    PhMainWndTabControlOnSelectionChanged();

    // Perform a layout.
    SendMessage(PhMainWndHandle, WM_SIZE, 0, 0);

    PhStartProviderThread(&PhPrimaryProviderThread);
    PhStartProviderThread(&PhSecondaryProviderThread);

    UpdateWindow(PhMainWndHandle);

    if (PhGetIntegerSetting(L"MainWindowState") == SW_MAXIMIZE)
    {
        ShowWindow(PhMainWndHandle, SW_SHOWMAXIMIZED);
    }
    else
    {
        ShowWindow(PhMainWndHandle, ShowCommand);
    }

    // Register for WTS notifications.
    {
        _WTSRegisterSessionNotification WTSRegisterSessionNotification_I;

        WTSRegisterSessionNotification_I = (_WTSRegisterSessionNotification)GetProcAddress(
            GetModuleHandle(L"wtsapi32.dll"),
            "WTSRegisterSessionNotification"
            );

        if (WTSRegisterSessionNotification_I)
            WTSRegisterSessionNotification_I(PhMainWndHandle, NOTIFY_FOR_ALL_SESSIONS);
    }

    PhpRefreshUsersMenu();

    return TRUE;
}

LRESULT CALLBACK PhMainWndProc(      
    __in HWND hWnd,
    __in UINT uMsg,
    __in WPARAM wParam,
    __in LPARAM lParam
    )
{
    switch (uMsg)
    { 
    case WM_DESTROY:
        {
            if (!PhMainWndExiting)
                PhpSaveAllSettings();

            PostQuitMessage(0);
        }
        break;
    case WM_SETTINGCHANGE:
        {
            PhReloadSysParameters();
        }
        break;
    case WM_COMMAND:
        {
            INT id = LOWORD(wParam);

            switch (id)
            {
            case ID_HACKER_RUN:
                {
                    if (RunFileDlg)
                    {
                        SelectedRunAsAdmin = FALSE;
                        RunFileDlg(hWnd, NULL, NULL, NULL, NULL, 0);
                    }
                }
                break;
            case ID_HACKER_RUNASADMINISTRATOR:
                {
                    if (RunFileDlg)
                    {
                        SelectedRunAsAdmin = TRUE;
                        RunFileDlg(
                            hWnd,
                            NULL,
                            NULL,
                            NULL, 
                            L"Type the name of a program that will be opened under alternate credentials.",
                            0
                            );
                    }
                }
                break;
            case ID_HACKER_RUNAS:
                {
                    PhShowRunAsDialog(hWnd, NULL);
                }
                break;
            case ID_HACKER_SHOWDETAILSFORALLPROCESSES:
                {
                    ProcessHacker_PrepareForEarlyShutdown(hWnd);

                    if (PhShellExecuteEx(hWnd, PhApplicationFileName->Buffer,
                        L"", SW_SHOW, PH_SHELL_EXECUTE_ADMIN, 0, NULL))
                    {
                        ProcessHacker_Destroy(hWnd);
                    }
                    else
                    {
                        ProcessHacker_CancelEarlyShutdown(hWnd);
                    }
                }
                break;
            case ID_HACKER_SAVE:
                {
                    static PH_FILETYPE_FILTER filters[] =
                    {
                        { L"Text files (*.txt;*.log)", L"*.txt;*.log" },
                        { L"All files (*.*)", L"*.*" }
                    };
                    PVOID saveFileDialog = PhCreateSaveFileDialog();

                    PhSetFileDialogFilter(saveFileDialog, filters, sizeof(filters) / sizeof(PH_FILETYPE_FILTER));

                    if (PhShowFileDialog(hWnd, saveFileDialog))
                    {
                        PPH_STRING fileName;

                        fileName = PhGetFileDialogFileName(saveFileDialog);

                        PhShowMessage(
                            hWnd,
                            MB_ICONINFORMATION,
                            L"You selected:\n\n%s",
                            fileName->Buffer
                            );

                        PhDereferenceObject(fileName);
                    }

                    PhFreeFileDialog(saveFileDialog);
                }
                break;
            case ID_HACKER_FINDHANDLESORDLLS:
                {
                    PhShowFindObjectsDialog();
                }
                break;
            case ID_HACKER_OPTIONS:
                {
                    PhShowOptionsDialog(hWnd);
                }
                break;
            case ID_HACKER_EXIT:
                ProcessHacker_Destroy(hWnd);
                break;
            case ID_VIEW_REFRESH:
                {
                    PhBoostProvider(&ProcessProviderRegistration, NULL);
                    PhBoostProvider(&ServiceProviderRegistration, NULL);
                }
                break;
            case ID_TOOLS_HIDDENPROCESSES:
                {
                    PhShowHiddenProcessesDialog();
                }
                break;
            case ID_TOOLS_PAGEFILES:
                {
                    PhShowPagefilesDialog(hWnd);
                }
                break;
            case ID_TOOLS_VERIFYFILESIGNATURE:
                {
                    static PH_FILETYPE_FILTER filters[] =
                    {
                        { L"Executable files (*.exe;*.dll;*.ocx;*.sys;*.scr;*.cpl)", L"*.exe;*.dll;*.ocx;*.sys;*.scr;*.cpl" },
                        { L"All files (*.*)", L"*.*" }
                    };
                    PVOID fileDialog = PhCreateOpenFileDialog();

                    PhSetFileDialogFilter(fileDialog, filters, sizeof(filters) / sizeof(PH_FILETYPE_FILTER));

                    if (PhShowFileDialog(hWnd, fileDialog))
                    {
                        PPH_STRING fileName;
                        VERIFY_RESULT result;
                        PPH_STRING signerName;

                        fileName = PhGetFileDialogFileName(fileDialog);
                        result = PhVerifyFile(fileName->Buffer, &signerName);

                        if (result == VrTrusted)
                        {
                            PhShowInformation(hWnd, L"\"%s\" is trusted and signed by \"%s\".",
                                fileName->Buffer, signerName->Buffer);
                        }
                        else if (result == VrNoSignature)
                        {
                            PhShowInformation(hWnd, L"\"%s\" does not have a digital signature.",
                                fileName->Buffer);
                        }
                        else
                        {
                            PhShowInformation(hWnd, L"\"%s\" is not trusted.",
                                fileName->Buffer);
                        }

                        if (signerName)
                            PhDereferenceObject(signerName);

                        PhDereferenceObject(fileName);
                    }

                    PhFreeFileDialog(fileDialog);
                }
                break;
            case ID_USER_CONNECT:
                {
                    PhUiConnectSession(hWnd, SelectedUserSessionId);
                }
                break;
            case ID_USER_DISCONNECT:
                {
                    PhUiDisconnectSession(hWnd, SelectedUserSessionId);
                }
                break;
            case ID_USER_LOGOFF:
                {
                    PhUiLogoffSession(hWnd, SelectedUserSessionId);
                }
                break;
            case ID_USER_SENDMESSAGE:
                {
                    PhShowSessionSendMessageDialog(hWnd, SelectedUserSessionId);
                }
                break;
            case ID_USER_PROPERTIES:
                {
                    PhShowSessionProperties(hWnd, SelectedUserSessionId);
                }
                break;
            case ID_HELP_DEBUGCONSOLE:
                {
                    PhShowDebugConsole();
                }
                break;
            case ID_HELP_ABOUT:
                {
                    PhShowAboutDialog(hWnd);
                }
                break;
            case ID_PROCESS_TERMINATE:
                {
                    PPH_PROCESS_ITEM *processes;
                    ULONG numberOfProcesses;

                    PhpGetSelectedProcesses(&processes, &numberOfProcesses);
                    PhReferenceObjects(processes, numberOfProcesses);

                    if (PhUiTerminateProcesses(hWnd, processes, numberOfProcesses))
                        PhDeselectAllProcessItems();

                    PhDereferenceObjects(processes, numberOfProcesses);
                    PhFree(processes);
                }
                break;
            case ID_PROCESS_TERMINATETREE:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        PhReferenceObject(processItem);

                        if (PhUiTerminateTreeProcess(hWnd, processItem))
                            PhDeselectAllProcessItems();

                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_PROCESS_SUSPEND:
                {
                    PPH_PROCESS_ITEM *processes;
                    ULONG numberOfProcesses;

                    PhpGetSelectedProcesses(&processes, &numberOfProcesses);
                    PhReferenceObjects(processes, numberOfProcesses);
                    PhUiSuspendProcesses(hWnd, processes, numberOfProcesses);
                    PhDereferenceObjects(processes, numberOfProcesses);
                    PhFree(processes);
                }
                break;
            case ID_PROCESS_RESUME:
                {
                    PPH_PROCESS_ITEM *processes;
                    ULONG numberOfProcesses;

                    PhpGetSelectedProcesses(&processes, &numberOfProcesses);
                    PhReferenceObjects(processes, numberOfProcesses);
                    PhUiResumeProcesses(hWnd, processes, numberOfProcesses);
                    PhDereferenceObjects(processes, numberOfProcesses);
                    PhFree(processes);
                }
                break;
            case ID_PROCESS_RESTART:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        PhReferenceObject(processItem);

                        if (PhUiRestartProcess(hWnd, processItem))
                            PhDeselectAllProcessItems();

                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_PROCESS_DEBUG:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        PhReferenceObject(processItem);
                        PhUiDebugProcess(hWnd, processItem);
                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_PROCESS_REDUCEWORKINGSET:
                {
                    PPH_PROCESS_ITEM *processes;
                    ULONG numberOfProcesses;

                    PhpGetSelectedProcesses(&processes, &numberOfProcesses);
                    PhReferenceObjects(processes, numberOfProcesses);
                    PhUiReduceWorkingSetProcesses(hWnd, processes, numberOfProcesses);
                    PhDereferenceObjects(processes, numberOfProcesses);
                    PhFree(processes);
                }
                break;
            case ID_PROCESS_VIRTUALIZATION:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        PhReferenceObject(processItem);
                        PhUiSetVirtualizationProcess(
                            hWnd,
                            processItem,
                            !SelectedProcessVirtualizationEnabled
                            );
                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_PROCESS_AFFINITY:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        PhReferenceObject(processItem);
                        PhShowProcessAffinityDialog(hWnd, processItem);
                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_PROCESS_CREATEDUMPFILE:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        PhReferenceObject(processItem);
                        PhUiCreateDumpFileProcess(hWnd, processItem);
                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_PROCESS_TERMINATOR:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        // The object relies on the list view reference, which could 
                        // disappear if we don't reference the object here.
                        PhReferenceObject(processItem);
                        PhShowProcessTerminatorDialog(hWnd, processItem);
                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_MISCELLANEOUS_DETACHFROMDEBUGGER:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        PhReferenceObject(processItem);
                        PhUiDetachFromDebuggerProcess(hWnd, processItem);
                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_MISCELLANEOUS_HEAPS:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        PhReferenceObject(processItem);
                        PhShowProcessHeapsDialog(hWnd, processItem);
                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_MISCELLANEOUS_INJECTDLL:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        PhReferenceObject(processItem);
                        PhUiInjectDllProcess(hWnd, processItem);
                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_I_0:
            case ID_I_1:
            case ID_I_2:
            case ID_I_3:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        ULONG ioPriority;

                        switch (id)
                        {
                            case ID_I_0:
                                ioPriority = 0;
                                break;
                            case ID_I_1:
                                ioPriority = 1;
                                break;
                            case ID_I_2:
                                ioPriority = 2;
                                break;
                            case ID_I_3:
                                ioPriority = 3;
                                break;
                        }

                        PhReferenceObject(processItem);
                        PhUiSetIoPriorityProcess(hWnd, processItem, ioPriority);
                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_PROCESS_PROPERTIES:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        // No reference needed; no messages pumped.
                        PhpShowProcessProperties(processItem);
                    }
                }
                break;
            case ID_PRIORITY_REALTIME:
            case ID_PRIORITY_HIGH:
            case ID_PRIORITY_ABOVENORMAL:
            case ID_PRIORITY_NORMAL:
            case ID_PRIORITY_BELOWNORMAL:
            case ID_PRIORITY_IDLE:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        ULONG priorityClassWin32;

                        switch (id)
                        {
                            case ID_PRIORITY_REALTIME:
                                priorityClassWin32 = REALTIME_PRIORITY_CLASS;
                                break;
                            case ID_PRIORITY_HIGH:
                                priorityClassWin32 = HIGH_PRIORITY_CLASS;
                                break;
                            case ID_PRIORITY_ABOVENORMAL:
                                priorityClassWin32 = ABOVE_NORMAL_PRIORITY_CLASS;
                                break;
                            case ID_PRIORITY_NORMAL:
                                priorityClassWin32 = NORMAL_PRIORITY_CLASS;
                                break;
                            case ID_PRIORITY_BELOWNORMAL:
                                priorityClassWin32 = BELOW_NORMAL_PRIORITY_CLASS;
                                break;
                            case ID_PRIORITY_IDLE:
                                priorityClassWin32 = IDLE_PRIORITY_CLASS;
                                break;
                        }

                        PhReferenceObject(processItem);
                        PhUiSetPriorityProcess(hWnd, processItem, priorityClassWin32);
                        PhDereferenceObject(processItem);
                    }
                }
                break;
            case ID_WINDOW_BRINGTOFRONT:
                {
                    if (IsWindow(SelectedProcessWindowHandle))
                    {
                        WINDOWPLACEMENT placement = { sizeof(placement) };

                        GetWindowPlacement(SelectedProcessWindowHandle, &placement);

                        if (placement.showCmd == SW_MINIMIZE)
                            ShowWindow(SelectedProcessWindowHandle, SW_RESTORE);
                        else
                            SetForegroundWindow(SelectedProcessWindowHandle);
                    }
                }
                break;
            case ID_WINDOW_RESTORE:
                {
                    if (IsWindow(SelectedProcessWindowHandle))
                    {
                        ShowWindow(SelectedProcessWindowHandle, SW_RESTORE);
                    }
                }
                break;
            case ID_WINDOW_MINIMIZE:
                {
                    if (IsWindow(SelectedProcessWindowHandle))
                    {
                        ShowWindow(SelectedProcessWindowHandle, SW_MINIMIZE);
                    }
                }
                break;
            case ID_WINDOW_MAXIMIZE:
                {
                    if (IsWindow(SelectedProcessWindowHandle))
                    {
                        ShowWindow(SelectedProcessWindowHandle, SW_MAXIMIZE);
                    }
                }
                break;
            case ID_WINDOW_CLOSE:
                {
                    if (IsWindow(SelectedProcessWindowHandle))
                    {
                        PostMessage(SelectedProcessWindowHandle, WM_CLOSE, 0, 0);
                    }
                }
                break;
            case ID_PROCESS_SEARCHONLINE:
                {
                    PPH_PROCESS_ITEM processItem = PhpGetSelectedProcess();

                    if (processItem)
                    {
                        PhSearchOnlineString(hWnd, processItem->ProcessName->Buffer);
                    }
                }
                break;
            case ID_SERVICE_START:
                {
                    PPH_SERVICE_ITEM serviceItem = PhpGetSelectedService();

                    if (serviceItem)
                    {
                        PhReferenceObject(serviceItem);
                        PhUiStartService(hWnd, serviceItem);
                        PhDereferenceObject(serviceItem);
                    }
                }
                break;
            case ID_SERVICE_CONTINUE:
                {
                    PPH_SERVICE_ITEM serviceItem = PhpGetSelectedService();

                    if (serviceItem)
                    {
                        PhReferenceObject(serviceItem);
                        PhUiContinueService(hWnd, serviceItem);
                        PhDereferenceObject(serviceItem);
                    }
                }
                break;
            case ID_SERVICE_PAUSE:
                {
                    PPH_SERVICE_ITEM serviceItem = PhpGetSelectedService();

                    if (serviceItem)
                    {
                        PhReferenceObject(serviceItem);
                        PhUiPauseService(hWnd, serviceItem);
                        PhDereferenceObject(serviceItem);
                    }
                }
                break;
            case ID_SERVICE_STOP:
                {
                    PPH_SERVICE_ITEM serviceItem = PhpGetSelectedService();

                    if (serviceItem)
                    {
                        PhReferenceObject(serviceItem);
                        PhUiStopService(hWnd, serviceItem);
                        PhDereferenceObject(serviceItem);
                    }
                }
                break;
            case ID_SERVICE_DELETE:
                {
                    PPH_SERVICE_ITEM serviceItem = PhpGetSelectedService();

                    if (serviceItem)
                    {
                        PhReferenceObject(serviceItem);
                        PhUiDeleteService(hWnd, serviceItem);
                        PhDereferenceObject(serviceItem);
                    }
                }
                break;
            case ID_SERVICE_PROPERTIES:
                {
                    PPH_SERVICE_ITEM serviceItem = PhpGetSelectedService();

                    if (serviceItem)
                    {
                        // The object relies on the list view reference, which could 
                        // disappear if we don't reference the object here.
                        PhReferenceObject(serviceItem);
                        PhShowServiceProperties(hWnd, serviceItem);
                        PhDereferenceObject(serviceItem);
                    }
                }
                break;
            }
        }
        break;
    case WM_SYSCOMMAND:
        {
            switch (wParam)
            {
            case SC_MINIMIZE:
                {
                    // Save the current window state because we 
                    // may not have a chance to later.
                    PhpSaveWindowState();
                }
                break;
            }
        }
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    case WM_MENUSELECT:
        {
            switch (LOWORD(wParam))
            {
            case ID_USER_CONNECT:
            case ID_USER_DISCONNECT:
            case ID_USER_LOGOFF:
            case ID_USER_SENDMESSAGE:
            case ID_USER_PROPERTIES:
                {
                    MENUITEMINFO menuItemInfo = { sizeof(menuItemInfo) };

                    menuItemInfo.fMask = MIIM_DATA;

                    if (GetMenuItemInfo((HMENU)lParam, LOWORD(wParam), FALSE, &menuItemInfo))
                    {
                        SelectedUserSessionId = (ULONG)menuItemInfo.dwItemData;
                    }
                }
                break;
            }
        }
        break;
    case WM_SIZE:
        {
            if (!IsIconic(hWnd))
            {
                HDWP deferHandle = BeginDeferWindowPos(2);
                PhMainWndOnLayout(&deferHandle);
                EndDeferWindowPos(deferHandle);
            }
        }
        break;
    case WM_SIZING:
        {
            PhResizingMinimumSize((PRECT)lParam, wParam, 400, 340);
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            if (header->hwndFrom == TabControlHandle)
            {
                PhMainWndTabControlOnNotify(header);
            }
            else if (header->hwndFrom == ServiceListViewHandle)
            {
                PhMainWndServiceListViewOnNotify(header);
            }
            else if (header->code == RFN_VALIDATE && SelectedRunAsAdmin)
            {
                LPNMRUNFILEDLG runFileDlg = (LPNMRUNFILEDLG)header;

                if (PhShellExecuteEx(hWnd, (PWSTR)runFileDlg->lpszFile,
                    NULL, runFileDlg->nShow, PH_SHELL_EXECUTE_ADMIN, 0, NULL))
                {
                    return RF_CANCEL;
                }
                else
                {
                    return RF_RETRY;
                }
            }
        }
        break;
    case WM_WTSSESSION_CHANGE:
        {
            if (wParam == WTS_SESSION_LOGON || wParam == WTS_SESSION_LOGOFF)
            {
                PhpRefreshUsersMenu();
            }
        }
        break;
    case WM_PH_ACTIVATE:
        {
            if (!PhMainWndExiting)
            {
                if (IsIconic(hWnd))
                {
                    ShowWindow(hWnd, SW_RESTORE);
                }

                return PH_ACTIVATE_REPLY;
            }
            else
            {
                return 0;
            }
        }
        break;
    case WM_PH_SHOW_PROCESS_PROPERTIES:
        {
            PhpShowProcessProperties((PPH_PROCESS_ITEM)lParam);
        }
        break;
    case WM_PH_DESTROY:
        {
            DestroyWindow(hWnd);
        }
        break;
    case WM_PH_SAVE_ALL_SETTINGS:
        {
            PhpSaveAllSettings();
        }
        break;
    case WM_PH_PREPARE_FOR_EARLY_SHUTDOWN:
        {
            PhpPrepareForEarlyShutdown();
        }
        break;
    case WM_PH_CANCEL_EARLY_SHUTDOWN:
        {
            PhpCancelEarlyShutdown();
        }
        break;
    case WM_PH_PROCESS_ADDED:
        {
            PPH_PROCESS_ITEM processItem = (PPH_PROCESS_ITEM)lParam;

            PhMainWndOnProcessAdded(processItem);
        }
        break;
    case WM_PH_PROCESS_MODIFIED:
        {
            PhMainWndOnProcessModified((PPH_PROCESS_ITEM)lParam);
        }
        break;
    case WM_PH_PROCESS_REMOVED:
        {
            PhMainWndOnProcessRemoved((PPH_PROCESS_ITEM)lParam);
        }
        break;
    case WM_PH_SERVICE_ADDED:
        {
            ULONG runId = (ULONG)wParam;
            PPH_SERVICE_ITEM serviceItem = (PPH_SERVICE_ITEM)lParam;

            PhMainWndOnServiceAdded(runId, serviceItem);
            PhDereferenceObject(serviceItem);
        }
        break;
    case WM_PH_SERVICE_MODIFIED:
        {
            PPH_SERVICE_MODIFIED_DATA serviceModifiedData = (PPH_SERVICE_MODIFIED_DATA)lParam;

            PhMainWndOnServiceModified(serviceModifiedData);
            PhFree(serviceModifiedData);
        }
        break;
    case WM_PH_SERVICE_REMOVED:
        {
            PhMainWndOnServiceRemoved((PPH_SERVICE_ITEM)lParam);
        }
        break;
    case WM_PH_SERVICES_UPDATED:
        {
            PhMainWndOnServicesUpdated();
        }
        break;
    }

    REFLECT_MESSAGE(ServiceListViewHandle, uMsg, wParam, lParam);

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

VOID PhpReloadListViewFont()
{
    HFONT fontHandle;
    LOGFONT font;

    if (ServiceListViewHandle && (fontHandle = (HFONT)SendMessage(ServiceListViewHandle, WM_GETFONT, 0, 0)))
    {
        if (GetObject(fontHandle, sizeof(LOGFONT), &font))
        {
            font.lfWeight = FW_BOLD;
            fontHandle = CreateFontIndirect(&font);

            if (fontHandle)
            {
                if (PhBoldListViewFont)
                    DeleteObject(PhBoldListViewFont);

                PhBoldListViewFont = fontHandle;
            }
        }
    }
}

VOID PhReloadSysParameters()
{
    PhSysWindowColor = GetSysColor(COLOR_WINDOW);

    DeleteObject(PhApplicationFont);
    DeleteObject(PhBoldMessageFont);
    PhInitializeFont(PhMainWndHandle);
    SendMessage(TabControlHandle, WM_SETFONT, (WPARAM)PhApplicationFont, FALSE);

    PhpReloadListViewFont();
}

VOID PhpSaveWindowState()
{
    WINDOWPLACEMENT placement = { sizeof(placement) };

    GetWindowPlacement(PhMainWndHandle, &placement);

    if (placement.showCmd == SW_NORMAL)
        PhSetIntegerSetting(L"MainWindowState", SW_NORMAL);
    else if (placement.showCmd == SW_MAXIMIZE)
        PhSetIntegerSetting(L"MainWindowState", SW_MAXIMIZE);
}

VOID PhpSaveAllSettings()
{
    WINDOWPLACEMENT placement = { sizeof(placement) };
    PH_RECTANGLE windowRectangle;

    GetWindowPlacement(PhMainWndHandle, &placement);
    windowRectangle = PhRectToRectangle(placement.rcNormalPosition);

    PhSetIntegerPairSetting(L"MainWindowPosition", windowRectangle.Position);
    PhSetIntegerPairSetting(L"MainWindowSize", windowRectangle.Size);

    PhpSaveWindowState();

    if (PhSettingsFileName)
        PhSaveSettings(PhSettingsFileName->Buffer);
}

VOID PhpPrepareForEarlyShutdown()
{
    PhpSaveAllSettings();
    PhMainWndExiting = TRUE;
}

VOID PhpCancelEarlyShutdown()
{
    PhMainWndExiting = FALSE;
}

VOID PhpRefreshUsersMenu()
{
    HMENU menu;
    PWTS_SESSION_INFO sessions;
    ULONG numberOfSessions;
    ULONG i;
    ULONG j;
    MENUITEMINFO menuItemInfo = { sizeof(MENUITEMINFO) };

    menu = GetMenu(PhMainWndHandle);
    menu = GetSubMenu(menu, 3);

    // Delete all items in the Users menu.
    while (DeleteMenu(menu, 0, MF_BYPOSITION)) ;

    if (WTSEnumerateSessions(
        WTS_CURRENT_SERVER_HANDLE,
        0,
        1,
        &sessions,
        &numberOfSessions
        ))
    {
        for (i = 0; i < numberOfSessions; i++)
        {
            HMENU userMenu;
            PPH_STRING domainName;
            PPH_STRING userName;
            PPH_STRING menuText;
            ULONG numberOfItems;

            domainName = PHA_DEREFERENCE(PhGetSessionInformationString(
                WTS_CURRENT_SERVER_HANDLE,
                sessions[i].SessionId,
                WTSDomainName
                ));
            userName = PHA_DEREFERENCE(PhGetSessionInformationString(
                WTS_CURRENT_SERVER_HANDLE,
                sessions[i].SessionId,
                WTSUserName
                ));

            if (PhIsStringNullOrEmpty(domainName) || PhIsStringNullOrEmpty(userName))
            {
                // Probably the Services or RDP-Tcp session.
                continue;
            }

            menuText = PhaFormatString(
                L"%u: %s\\%s",
                sessions[i].SessionId,
                domainName->Buffer,
                userName->Buffer
                );

            userMenu = GetSubMenu(LoadMenu(PhInstanceHandle, MAKEINTRESOURCE(IDR_USER)), 0);
            AppendMenu(
                menu,
                MF_STRING | MF_POPUP,
                (UINT_PTR)userMenu,
                menuText->Buffer
                );

            menuItemInfo.fMask = MIIM_DATA;
            menuItemInfo.dwItemData = sessions[i].SessionId;

            numberOfItems = GetMenuItemCount(userMenu);

            if (numberOfItems != -1)
            {
                for (j = 0; j < numberOfItems; j++)
                    SetMenuItemInfo(userMenu, j, TRUE, &menuItemInfo);
            }
        }

        WTSFreeMemory(sessions);
    }

    DrawMenuBar(PhMainWndHandle);
}

PPH_PROCESS_ITEM PhpGetSelectedProcess()
{
    return PhGetSelectedProcessItem();
}

VOID PhpGetSelectedProcesses(
    __out PPH_PROCESS_ITEM **Processes,
    __out PULONG NumberOfProcesses
    )
{
    PhGetSelectedProcessItems(Processes, NumberOfProcesses);
}

VOID PhpShowProcessProperties(
    __in PPH_PROCESS_ITEM ProcessItem
    )
{
    PPH_PROCESS_PROPCONTEXT propContext;

    propContext = PhCreateProcessPropContext(
        PhMainWndHandle,
        ProcessItem
        );

    if (propContext)
    {
        PhShowProcessProperties(propContext);
        PhDereferenceObject(propContext);
    }
}

PPH_SERVICE_ITEM PhpGetSelectedService()
{
    return PhGetSelectedListViewItemParam(
        ServiceListViewHandle
        );
}

VOID PhpGetSelectedServices(
    __out PPH_SERVICE_ITEM **Services,
    __out PULONG NumberOfServices
    )
{
    PhGetSelectedListViewItemParams(
        ServiceListViewHandle,
        Services,
        NumberOfServices
        );
}

static VOID NTAPI ProcessAddedHandler(
    __in PVOID Parameter,
    __in PVOID Context
    )
{
    PPH_PROCESS_ITEM processItem = (PPH_PROCESS_ITEM)Parameter;

    // Reference the process item so it doesn't get deleted before 
    // we handle the event in the main thread.
    PhReferenceObject(processItem);
    PostMessage(PhMainWndHandle, WM_PH_PROCESS_ADDED, 0, (LPARAM)processItem);
}

static VOID NTAPI ProcessModifiedHandler(
    __in PVOID Parameter,
    __in PVOID Context
    )
{
    PPH_PROCESS_ITEM processItem = (PPH_PROCESS_ITEM)Parameter;

    PostMessage(PhMainWndHandle, WM_PH_PROCESS_MODIFIED, 0, (LPARAM)processItem);
}

static VOID NTAPI ProcessRemovedHandler(
    __in PVOID Parameter,
    __in PVOID Context
    )
{
    PPH_PROCESS_ITEM processItem = (PPH_PROCESS_ITEM)Parameter;

    // We already have a reference to the process item, so we don't need to 
    // reference it here.
    PostMessage(PhMainWndHandle, WM_PH_PROCESS_REMOVED, 0, (LPARAM)processItem);
}

static VOID NTAPI ServiceAddedHandler(
    __in PVOID Parameter,
    __in PVOID Context
    )
{
    PPH_SERVICE_ITEM serviceItem = (PPH_SERVICE_ITEM)Parameter;

    PhReferenceObject(serviceItem);
    PostMessage(
        PhMainWndHandle,
        WM_PH_SERVICE_ADDED,
        PhGetProviderRunId(&ServiceProviderRegistration),
        (LPARAM)serviceItem
        );
}

static VOID NTAPI ServiceModifiedHandler(
    __in PVOID Parameter,
    __in PVOID Context
    )
{
    PPH_SERVICE_MODIFIED_DATA serviceModifiedData = (PPH_SERVICE_MODIFIED_DATA)Parameter;
    PPH_SERVICE_MODIFIED_DATA copy;

    copy = PhAllocateCopy(serviceModifiedData, sizeof(PH_SERVICE_MODIFIED_DATA));

    PostMessage(PhMainWndHandle, WM_PH_SERVICE_MODIFIED, 0, (LPARAM)copy);
}

static VOID NTAPI ServiceRemovedHandler(
    __in PVOID Parameter,
    __in PVOID Context
    )
{
    PPH_SERVICE_ITEM serviceItem = (PPH_SERVICE_ITEM)Parameter;

    PostMessage(PhMainWndHandle, WM_PH_SERVICE_REMOVED, 0, (LPARAM)serviceItem);
}

static VOID NTAPI ServicesUpdatedHandler(
    __in PVOID Parameter,
    __in PVOID Context
    )
{
    PostMessage(PhMainWndHandle, WM_PH_SERVICES_UPDATED, 0, 0);
}

VOID PhMainWndOnCreate()
{
    TabControlHandle = PhCreateTabControl(PhMainWndHandle);
    BringWindowToTop(TabControlHandle);
    ProcessesTabIndex = PhAddTabControlTab(TabControlHandle, 0, L"Processes");
    ServicesTabIndex = PhAddTabControlTab(TabControlHandle, 1, L"Services");
    NetworkTabIndex = PhAddTabControlTab(TabControlHandle, 2, L"Network");

    ProcessTreeListHandle = PhCreateTreeListControl(PhMainWndHandle, ID_MAINWND_PROCESSTL);
    BringWindowToTop(ProcessTreeListHandle);

    ServiceListViewHandle = PhCreateListViewControl(PhMainWndHandle, ID_MAINWND_SERVICELV);
    PhSetListViewStyle(ServiceListViewHandle, TRUE, TRUE);
    BringWindowToTop(ServiceListViewHandle);
    PhpReloadListViewFont();

    NetworkListViewHandle = PhCreateListViewControl(PhMainWndHandle, ID_MAINWND_NETWORKLV);
    PhSetListViewStyle(NetworkListViewHandle, TRUE, TRUE);
    BringWindowToTop(NetworkListViewHandle);

    PhSetControlTheme(ServiceListViewHandle, L"explorer");
    PhSetControlTheme(NetworkListViewHandle, L"explorer");

    PhAddListViewColumn(ServiceListViewHandle, 0, 0, 0, LVCFMT_LEFT, 100, L"Name");
    PhAddListViewColumn(ServiceListViewHandle, 1, 1, 1, LVCFMT_LEFT, 140, L"Display Name");
    PhAddListViewColumn(ServiceListViewHandle, 2, 2, 2, LVCFMT_LEFT, 50, L"PID");

    PhAddListViewColumn(NetworkListViewHandle, 0, 0, 0, LVCFMT_LEFT, 100, L"Process Name");

    PhProcessTreeListInitialization();
    PhInitializeProcessTreeList(ProcessTreeListHandle);

    PhSetExtendedListView(ServiceListViewHandle);
    ExtendedListView_SetStateHighlighting(ServiceListViewHandle, TRUE);

    PhRegisterCallback(
        &PhProcessAddedEvent,
        ProcessAddedHandler,
        NULL,
        &ProcessAddedRegistration
        );
    PhRegisterCallback(
        &PhProcessModifiedEvent,
        ProcessModifiedHandler,
        NULL,
        &ProcessModifiedRegistration
        );
    PhRegisterCallback(
        &PhProcessRemovedEvent,
        ProcessRemovedHandler,
        NULL,
        &ProcessRemovedRegistration
        );

    PhRegisterCallback(
        &PhServiceAddedEvent,
        ServiceAddedHandler,
        NULL,
        &ServiceAddedRegistration
        );
    PhRegisterCallback(
        &PhServiceModifiedEvent,
        ServiceModifiedHandler,
        NULL,
        &ServiceModifiedRegistration
        );
    PhRegisterCallback(
        &PhServiceRemovedEvent,
        ServiceRemovedHandler,
        NULL,
        &ServiceRemovedRegistration
        );
    PhRegisterCallback(
        &PhServicesUpdatedEvent,
        ServicesUpdatedHandler,
        NULL,
        &ServicesUpdatedRegistration
        );
}

VOID PhMainWndOnLayout(HDWP *deferHandle)
{
    RECT rect;

    // Resize the tab control.
    GetClientRect(PhMainWndHandle, &rect);

    // Don't defer the resize. The tab control doesn't repaint properly.
    SetWindowPos(TabControlHandle, NULL,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOACTIVATE | SWP_NOZORDER);
    UpdateWindow(TabControlHandle);

    PhMainWndTabControlOnLayout(deferHandle);
}

VOID PhMainWndTabControlOnLayout(HDWP *deferHandle)
{
    RECT rect;
    INT selectedIndex;

    GetClientRect(PhMainWndHandle, &rect);
    TabCtrl_AdjustRect(TabControlHandle, FALSE, &rect);

    selectedIndex = TabCtrl_GetCurSel(TabControlHandle);

    if (selectedIndex == ProcessesTabIndex)
    {
        *deferHandle = DeferWindowPos(*deferHandle, ProcessTreeListHandle, NULL, 
            rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
            SWP_NOACTIVATE | SWP_NOZORDER);
    }
    else if (selectedIndex == ServicesTabIndex)
    {
        *deferHandle = DeferWindowPos(*deferHandle, ServiceListViewHandle, NULL,
            rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
            SWP_NOACTIVATE | SWP_NOZORDER);
    }
    else if (selectedIndex == NetworkTabIndex)
    {
        *deferHandle = DeferWindowPos(*deferHandle, NetworkListViewHandle, NULL,
            rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
            SWP_NOACTIVATE | SWP_NOZORDER);
    }
}

VOID PhMainWndTabControlOnNotify(
    __in LPNMHDR Header
    )
{
    if (Header->code == TCN_SELCHANGE)
    {
        PhMainWndTabControlOnSelectionChanged();
    }
}

VOID PhMainWndTabControlOnSelectionChanged()
{
    INT selectedIndex;

    selectedIndex = TabCtrl_GetCurSel(TabControlHandle);

    {
        HDWP deferHandle = BeginDeferWindowPos(1);
        PhMainWndTabControlOnLayout(&deferHandle);
        EndDeferWindowPos(deferHandle);
    }

    ShowWindow(ProcessTreeListHandle, selectedIndex == ProcessesTabIndex ? SW_SHOW : SW_HIDE);
    ShowWindow(ServiceListViewHandle, selectedIndex == ServicesTabIndex ? SW_SHOW : SW_HIDE);
    ShowWindow(NetworkListViewHandle, selectedIndex == NetworkTabIndex ? SW_SHOW : SW_HIDE);
}

BOOL CALLBACK PhpEnumProcessWindowsProc(
    __in HWND hwnd,
    __in LPARAM lParam
    )
{
    ULONG processId;

    if (!IsWindowVisible(hwnd))
        return TRUE;

    GetWindowThreadProcessId(hwnd, &processId);

    if (processId == (ULONG)lParam)
    {
        SelectedProcessWindowHandle = hwnd;
        return FALSE;
    }

    return TRUE;
}

VOID PhpInitializeProcessMenu(
    __in HMENU Menu,
    __in PPH_PROCESS_ITEM *Processes,
    __in ULONG NumberOfProcesses
    )
{
#define MISCELLANEOUS_MENU_INDEX 12
#define WINDOW_MENU_INDEX 14

    if (NumberOfProcesses == 0)
    {
        PhEnableAllMenuItems(Menu, FALSE);
    }
    else if (NumberOfProcesses == 1)
    {
        // All menu items are enabled by default.

        // If the user selected a fake process, disable all but 
        // a few menu items.
        if (
            Processes[0]->ProcessId == DPCS_PROCESS_ID ||
            Processes[0]->ProcessId == INTERRUPTS_PROCESS_ID
            )
        {
            PhEnableAllMenuItems(Menu, FALSE);
            EnableMenuItem(Menu, ID_PROCESS_PROPERTIES, MF_ENABLED);
            EnableMenuItem(Menu, ID_PROCESS_SEARCHONLINE, MF_ENABLED);
        }
    }
    else
    {
        ULONG menuItemsMultiEnabled[] =
        {
            ID_PROCESS_TERMINATE,
            ID_PROCESS_SUSPEND,
            ID_PROCESS_RESUME,
            ID_PROCESS_REDUCEWORKINGSET
        };
        ULONG i;

        PhEnableAllMenuItems(Menu, FALSE);

        // These menu items are capable of manipulating 
        // multiple processes.
        for (i = 0; i < sizeof(menuItemsMultiEnabled) / sizeof(ULONG); i++)
        {
            EnableMenuItem(Menu, menuItemsMultiEnabled[i], MF_ENABLED);
        }
    }

    // Remove irrelevant menu items.

    if (WindowsVersion < WINDOWS_VISTA)
    {
        HMENU miscMenu;

        // Remove I/O priority.
        miscMenu = GetSubMenu(Menu, MISCELLANEOUS_MENU_INDEX);
        DeleteMenu(miscMenu, 3, MF_BYPOSITION);
    }

    // Virtualization
    if (NumberOfProcesses == 1)
    {
        HANDLE processHandle;
        HANDLE tokenHandle;
        BOOLEAN allowed = FALSE;
        BOOLEAN enabled = FALSE;

        if (NT_SUCCESS(PhOpenProcess(
            &processHandle,
            ProcessQueryAccess,
            Processes[0]->ProcessId
            )))
        {
            if (NT_SUCCESS(PhOpenProcessToken(
                &tokenHandle,
                TOKEN_QUERY,
                processHandle
                )))
            {
                PhGetTokenIsVirtualizationAllowed(tokenHandle, &allowed);
                PhGetTokenIsVirtualizationEnabled(tokenHandle, &enabled);
                SelectedProcessVirtualizationEnabled = enabled;

                NtClose(tokenHandle);
            }

            NtClose(processHandle);
        }

        if (!allowed)
        {
            EnableMenuItem(Menu, ID_PROCESS_VIRTUALIZATION, MF_DISABLED | MF_GRAYED);
        }
        else
        {
            CheckMenuItem(Menu, ID_PROCESS_VIRTUALIZATION, enabled ? MF_CHECKED : MF_UNCHECKED);
        }
    }

    // Priority
    if (NumberOfProcesses == 1)
    {
        HANDLE processHandle;
        ULONG priorityClass = 0;
        ULONG ioPriority = -1;
        ULONG id = 0;

        if (NT_SUCCESS(PhOpenProcess(
            &processHandle,
            ProcessQueryAccess,
            Processes[0]->ProcessId
            )))
        {
            priorityClass = GetPriorityClass(processHandle);

            if (WindowsVersion >= WINDOWS_VISTA)
            {
                if (!NT_SUCCESS(PhGetProcessIoPriority(
                    processHandle,
                    &ioPriority
                    )))
                {
                    ioPriority = -1;
                }
            }

            NtClose(processHandle);
        }

        switch (priorityClass)
        {
        case REALTIME_PRIORITY_CLASS:
            id = ID_PRIORITY_REALTIME;
            break;
        case HIGH_PRIORITY_CLASS:
            id = ID_PRIORITY_HIGH;
            break;
        case ABOVE_NORMAL_PRIORITY_CLASS:
            id = ID_PRIORITY_ABOVENORMAL;
            break;
        case NORMAL_PRIORITY_CLASS:
            id = ID_PRIORITY_NORMAL;
            break;
        case BELOW_NORMAL_PRIORITY_CLASS:
            id = ID_PRIORITY_BELOWNORMAL;
            break;
        case IDLE_PRIORITY_CLASS:
            id = ID_PRIORITY_IDLE;
            break;
        }

        if (id != 0)
        {
            CheckMenuItem(Menu, id, MF_CHECKED);
            PhSetRadioCheckMenuItem(Menu, id, TRUE);
        }

        if (ioPriority != -1)
        {
            id = 0;

            switch (ioPriority)
            {
            case 0:
                id = ID_I_0;
                break;
            case 1:
                id = ID_I_1;
                break;
            case 2:
                id = ID_I_2;
                break;
            case 3:
                id = ID_I_3;
                break;
            }

            if (id != 0)
            {
                CheckMenuItem(Menu, id, MF_CHECKED);
                PhSetRadioCheckMenuItem(Menu, id, TRUE);
            }
        }
    }

    // Window menu
    if (NumberOfProcesses == 1)
    {
        WINDOWPLACEMENT placement = { sizeof(placement) };

        // Get a handle to the process' top-level window (if any).
        SelectedProcessWindowHandle = NULL;
        EnumWindows(PhpEnumProcessWindowsProc, (ULONG)Processes[0]->ProcessId);

        if (SelectedProcessWindowHandle)
        {
            EnableMenuItem(Menu, WINDOW_MENU_INDEX, MF_ENABLED | MF_BYPOSITION);
        }
        else
        {
            EnableMenuItem(Menu, WINDOW_MENU_INDEX, MF_DISABLED | MF_GRAYED | MF_BYPOSITION);
        }

        GetWindowPlacement(SelectedProcessWindowHandle, &placement);

        PhEnableAllMenuItems(GetSubMenu(Menu, WINDOW_MENU_INDEX), TRUE);

        if (placement.showCmd == SW_MINIMIZE)
            EnableMenuItem(Menu, ID_WINDOW_MINIMIZE, MF_DISABLED | MF_GRAYED);
        else if (placement.showCmd == SW_MAXIMIZE)
            EnableMenuItem(Menu, ID_WINDOW_MAXIMIZE, MF_DISABLED | MF_GRAYED);
        else if (placement.showCmd == SW_NORMAL)
            EnableMenuItem(Menu, ID_WINDOW_RESTORE, MF_DISABLED | MF_GRAYED);
    }
    else
    {
        EnableMenuItem(Menu, WINDOW_MENU_INDEX, MF_DISABLED | MF_GRAYED | MF_BYPOSITION);
    }

    // Remove irrelevant menu items (continued)

    if (!WINDOWS_HAS_UAC)
    {
        DeleteMenu(Menu, ID_PROCESS_VIRTUALIZATION, 0);
    }
}

VOID PhShowProcessContextMenu(
    __in POINT Location
    )
{
    PPH_PROCESS_ITEM *processes;
    ULONG numberOfProcesses;

    PhpGetSelectedProcesses(&processes, &numberOfProcesses);

    if (numberOfProcesses != 0)
    {
        HMENU menu;
        HMENU subMenu;

        menu = LoadMenu(PhInstanceHandle, MAKEINTRESOURCE(IDR_PROCESS));
        subMenu = GetSubMenu(menu, 0);

        SetMenuDefaultItem(subMenu, ID_PROCESS_PROPERTIES, FALSE);
        PhpInitializeProcessMenu(subMenu, processes, numberOfProcesses);

        PhShowContextMenu(
            PhMainWndHandle,
            ProcessTreeListHandle,
            subMenu,
            Location
            );
        DestroyMenu(menu);
    }

    PhFree(processes);
}

VOID PhpInitializeServiceMenu(
    __in HMENU Menu,
    __in PPH_SERVICE_ITEM *Services,
    __in ULONG NumberOfServices
    )
{
    if (NumberOfServices == 0)
    {
        PhEnableAllMenuItems(Menu, FALSE);
    }
    else if (NumberOfServices == 1)
    {
        // Nothing
    }
    else
    {
        // None of the menu items work with multiple items.
        PhEnableAllMenuItems(Menu, FALSE);
    }

    if (NumberOfServices == 1)
    {
        switch (Services[0]->State)
        {
        case SERVICE_RUNNING:
            {
                PhEnableMenuItem(Menu, ID_SERVICE_START, FALSE);
                PhEnableMenuItem(Menu, ID_SERVICE_CONTINUE, FALSE);
                PhEnableMenuItem(Menu, ID_SERVICE_PAUSE,
                    Services[0]->ControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE);
                PhEnableMenuItem(Menu, ID_SERVICE_STOP,
                    Services[0]->ControlsAccepted & SERVICE_ACCEPT_STOP);
            }
            break;
        case SERVICE_PAUSED:
            {
                PhEnableMenuItem(Menu, ID_SERVICE_START, FALSE);
                PhEnableMenuItem(Menu, ID_SERVICE_CONTINUE,
                    Services[0]->ControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE);
                PhEnableMenuItem(Menu, ID_SERVICE_PAUSE, FALSE);
                PhEnableMenuItem(Menu, ID_SERVICE_STOP,
                    Services[0]->ControlsAccepted & SERVICE_ACCEPT_STOP);
            }
            break;
        case SERVICE_STOPPED:
            {
                PhEnableMenuItem(Menu, ID_SERVICE_CONTINUE, FALSE);
                PhEnableMenuItem(Menu, ID_SERVICE_PAUSE, FALSE);
                PhEnableMenuItem(Menu, ID_SERVICE_STOP, FALSE);
            }
            break;
        case SERVICE_START_PENDING:
        case SERVICE_CONTINUE_PENDING:
        case SERVICE_PAUSE_PENDING:
        case SERVICE_STOP_PENDING:
            {
                PhEnableMenuItem(Menu, ID_SERVICE_START, FALSE);
                PhEnableMenuItem(Menu, ID_SERVICE_CONTINUE, FALSE);
                PhEnableMenuItem(Menu, ID_SERVICE_PAUSE, FALSE);
                PhEnableMenuItem(Menu, ID_SERVICE_STOP, FALSE);
            }
            break;
        }

        if (!(Services[0]->ControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE))
        {
            DeleteMenu(Menu, ID_SERVICE_CONTINUE, 0);
            DeleteMenu(Menu, ID_SERVICE_PAUSE, 0);
        }
    }
}

VOID PhMainWndServiceListViewOnNotify(
    __in LPNMHDR Header
    )
{
    switch (Header->code)
    {
    case NM_DBLCLK:
        {
            SendMessage(PhMainWndHandle, WM_COMMAND, ID_SERVICE_PROPERTIES, 0);
        }
        break;
    case NM_RCLICK:
        {
            LPNMITEMACTIVATE itemActivate = (LPNMITEMACTIVATE)Header;
            PPH_SERVICE_ITEM *services;
            ULONG numberOfServices;

            PhpGetSelectedServices(&services, &numberOfServices);

            if (numberOfServices != 0)
            {
                HMENU menu;
                HMENU subMenu;

                menu = LoadMenu(PhInstanceHandle, MAKEINTRESOURCE(IDR_SERVICE));
                subMenu = GetSubMenu(menu, 0);

                SetMenuDefaultItem(subMenu, ID_SERVICE_PROPERTIES, FALSE);
                PhpInitializeServiceMenu(subMenu, services, numberOfServices);

                PhShowContextMenu(
                    PhMainWndHandle,
                    ServiceListViewHandle,
                    subMenu,
                    itemActivate->ptAction
                    );
                DestroyMenu(menu);
            }

            PhFree(services);
        }
        break;
    case LVN_KEYDOWN:
        {
            LPNMLVKEYDOWN keyDown = (LPNMLVKEYDOWN)Header;

            switch (keyDown->wVKey)
            {
            case VK_DELETE:
                SendMessage(PhMainWndHandle, WM_COMMAND, ID_SERVICE_DELETE, 0);
                break;
            case VK_RETURN:
                SendMessage(PhMainWndHandle, WM_COMMAND, ID_SERVICE_PROPERTIES, 0);
                break;
            }
        }
        break;
    }
}

VOID PhMainWndOnProcessAdded(
    __in __assumeRefs(1) PPH_PROCESS_ITEM ProcessItem
    )
{
    PhCreateProcessNode(ProcessItem);
}

VOID PhMainWndOnProcessModified(
    __in PPH_PROCESS_ITEM ProcessItem
    )
{
    PhUpdateProcessNode(PhFindProcessNode(ProcessItem->ProcessId));
}

VOID PhMainWndOnProcessRemoved(
    __in PPH_PROCESS_ITEM ProcessItem
    )
{
    PhRemoveProcessNode(PhFindProcessNode(ProcessItem->ProcessId));

    // Remove the reference for the process item being displayed.
    PhDereferenceObject(ProcessItem);
}

VOID PhMainWndOnServiceAdded(
    __in ULONG RunId,
    __in PPH_SERVICE_ITEM ServiceItem
    )
{
    INT lvItemIndex;

    // Add a reference for the pointer being stored in the list view item.
    PhReferenceObject(ServiceItem);

    if (RunId == 1) ExtendedListView_SetStateHighlighting(ServiceListViewHandle, FALSE);
    lvItemIndex = PhAddListViewItem(
        ServiceListViewHandle,
        MAXINT,
        ServiceItem->Name->Buffer,
        ServiceItem
        );
    if (RunId == 1) ExtendedListView_SetStateHighlighting(ServiceListViewHandle, TRUE);
    PhSetListViewSubItem(ServiceListViewHandle, lvItemIndex, 1, PhGetString(ServiceItem->DisplayName));
    PhSetListViewSubItem(ServiceListViewHandle, lvItemIndex, 2, ServiceItem->ProcessIdString);
}

VOID PhMainWndOnServiceModified(
    __in PPH_SERVICE_MODIFIED_DATA ServiceModifiedData
    )
{
    INT lvItemIndex;

    lvItemIndex = PhFindListViewItemByParam(ServiceListViewHandle, -1, ServiceModifiedData->Service);
    PhSetListViewSubItem(ServiceListViewHandle, lvItemIndex, 2, ServiceModifiedData->Service->ProcessIdString);
}

VOID PhMainWndOnServiceRemoved(
    __in PPH_SERVICE_ITEM ServiceItem
    )
{
    PhRemoveListViewItem(
        ServiceListViewHandle,
        PhFindListViewItemByParam(ServiceListViewHandle, -1, ServiceItem)
        );
    // Remove the reference we added in PhMainWndOnServiceAdded.
    PhDereferenceObject(ServiceItem);
}

VOID PhMainWndOnServicesUpdated()
{
    ExtendedListView_Tick(ServiceListViewHandle);
}