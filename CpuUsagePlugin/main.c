/*
 * CPU Usage Scaled by Cores Plugin for Process Hacker
 */

#include "resource.h"
#include <phdk.h>
#include <settings.h>

#define COLUMN_ID_CPU_SCALED 1

typedef struct _PROCESS_EXTENSION
{
    LIST_ENTRY ListEntry;
    PPH_PROCESS_ITEM ProcessItem;

    FLOAT CpuUsage;
    WCHAR CpuUsageText[PH_INT32_STR_LEN_1];
} PROCESS_EXTENSION, * PPROCESS_EXTENSION;

PPH_PLUGIN PluginInstance;
PH_CALLBACK_REGISTRATION TreeNewMessageCallbackRegistration;
PH_CALLBACK_REGISTRATION ProcessTreeNewInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION ProcessAddedCallbackRegistration;
PH_CALLBACK_REGISTRATION ProcessRemovedCallbackRegistration;
PH_CALLBACK_REGISTRATION ProcessesUpdatedCallbackRegistration;

LIST_ENTRY ProcessListHead = { &ProcessListHead, &ProcessListHead };

VOID TreeNewMessageCallback(_In_opt_ PVOID Parameter, _In_opt_ PVOID Context)
{
    PPH_PLUGIN_TREENEW_MESSAGE message = Parameter;

    if (message->Message != TreeNewGetCellText ||
        message->SubId != COLUMN_ID_CPU_SCALED) {
        return;
    }

    PPH_TREENEW_GET_CELL_TEXT getCellText = message->Parameter1;
    PPH_PROCESS_NODE node;
    PPROCESS_EXTENSION extension;

    node = (PPH_PROCESS_NODE)getCellText->Node;
    extension = PhPluginGetObjectExtension(PluginInstance, node->ProcessItem,
                                           EmProcessItemType);

    PWCHAR buffer = extension->CpuUsageText;
    FLOAT cpuUsage =
        extension->CpuUsage * PhSystemBasicInformation.NumberOfProcessors * 100;

    if (cpuUsage >= 0.01) {
        PH_FORMAT format;
        SIZE_T returnLength;

        PhInitFormatF(&format, cpuUsage, 2);

        if (PhFormatToBuffer(&format, 1, buffer, PH_INT32_STR_LEN_1 * sizeof(WCHAR),
                             &returnLength)) {
            getCellText->Text.Buffer = buffer;
            getCellText->Text.Length = (USHORT)(returnLength - sizeof(WCHAR)); // minus null terminator
        }
    } else if (cpuUsage != 0 && PhGetIntegerSetting(L"ShowCpuBelow001")) {
        PhInitializeStringRef(&getCellText->Text, L"< 0.01");
    }
}

LONG NTAPI CpuSortFunction(_In_ PVOID Node1, _In_ PVOID Node2,
                           _In_ ULONG SubId, _In_ PH_SORT_ORDER SortOrder,
                           _In_ PVOID Context)
{
    PPH_PROCESS_NODE node1 = Node1;
    PPH_PROCESS_NODE node2 = Node2;
    PPROCESS_EXTENSION extension1 = PhPluginGetObjectExtension(PluginInstance,
                                                               node1->ProcessItem,
                                                               EmProcessItemType);
    PPROCESS_EXTENSION extension2 = PhPluginGetObjectExtension(PluginInstance,
                                                               node2->ProcessItem,
                                                               EmProcessItemType);

    return singlecmp(extension1->CpuUsage, extension2->CpuUsage);
}

VOID ProcessTreeNewInitializingCallback(_In_opt_ PVOID Parameter,
                                        _In_opt_ PVOID Context)
{
    PPH_PLUGIN_TREENEW_INFORMATION info = Parameter;
    PH_TREENEW_COLUMN column;

    memset(&column, 0, sizeof(PH_TREENEW_COLUMN));
    column.SortDescending = TRUE;
    column.Text = L"CPU (scaled)";
    column.Width = 45;
    column.Alignment = PH_ALIGN_RIGHT;
    column.TextFlags = DT_RIGHT;
    PhPluginAddTreeNewColumn(PluginInstance, info->CmData, &column,
                             COLUMN_ID_CPU_SCALED, NULL, CpuSortFunction);
}

VOID ProcessItemCreateCallback(_In_ PVOID Object,
                               _In_ PH_EM_OBJECT_TYPE ObjectType,
                               _In_ PVOID Extension)
{
    PPH_PROCESS_ITEM processItem = Object;
    PPROCESS_EXTENSION extension = Extension;

    memset(extension, 0, sizeof(PROCESS_EXTENSION));
    extension->ProcessItem = processItem;
}

VOID ProcessAddedHandler(_In_opt_ PVOID Parameter, _In_opt_ PVOID Context)
{
    PPH_PROCESS_ITEM processItem = Parameter;
    PPROCESS_EXTENSION extension = PhPluginGetObjectExtension(PluginInstance, processItem,
                                                              EmProcessItemType);

    InsertTailList(&ProcessListHead, &extension->ListEntry);
}

VOID ProcessRemovedHandler(_In_opt_ PVOID Parameter, _In_opt_ PVOID Context)
{
    PPH_PROCESS_ITEM processItem = Parameter;
    PPROCESS_EXTENSION extension = PhPluginGetObjectExtension(PluginInstance, processItem,
                                                              EmProcessItemType);

    RemoveEntryList(&extension->ListEntry);
}

VOID ProcessesUpdatedHandler(_In_opt_ PVOID Parameter, _In_opt_ PVOID Context)
{
    PLIST_ENTRY listEntry;

    listEntry = ProcessListHead.Flink;

    while (listEntry != &ProcessListHead) {
        PPROCESS_EXTENSION extension =
            CONTAINING_RECORD(listEntry, PROCESS_EXTENSION, ListEntry);
        PPH_PROCESS_ITEM processItem = extension->ProcessItem;

        extension->CpuUsage = processItem->CpuUsage;
        listEntry = listEntry->Flink;
    }
}

LOGICAL DllMain(_In_ HINSTANCE Instance, _In_ ULONG Reason,
                _Reserved_ PVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH) {
        PPH_PLUGIN_INFORMATION info;

        PluginInstance = PhRegisterPlugin(L"dllexport.CpuUsagePlugin", Instance, &info);

        if (!PluginInstance)
            return FALSE;

        info->DisplayName = L"CPU (scaled)";
        info->Description =
            L"Adds a column to display CPU times scaled by number of cores.";
        info->Author = L"dllexport";

        PhRegisterCallback(PhGetPluginCallback(PluginInstance, PluginCallbackTreeNewMessage),
                           TreeNewMessageCallback, NULL, &TreeNewMessageCallbackRegistration);
        PhRegisterCallback(PhGetGeneralCallback(GeneralCallbackProcessTreeNewInitializing),
                           ProcessTreeNewInitializingCallback, NULL,
                           &ProcessTreeNewInitializingCallbackRegistration);
        PhRegisterCallback(PhGetGeneralCallback(GeneralCallbackProcessProviderAddedEvent),
                           ProcessAddedHandler, NULL, &ProcessAddedCallbackRegistration);
        PhRegisterCallback(PhGetGeneralCallback(GeneralCallbackProcessProviderRemovedEvent),
                           ProcessRemovedHandler, NULL, &ProcessRemovedCallbackRegistration);
        PhRegisterCallback(PhGetGeneralCallback(GeneralCallbackProcessProviderUpdatedEvent),
                           ProcessesUpdatedHandler, NULL, &ProcessesUpdatedCallbackRegistration);

        PhPluginSetObjectExtension(PluginInstance, EmProcessItemType,
                                   sizeof(PROCESS_EXTENSION),
                                   ProcessItemCreateCallback, NULL);
    }

    return TRUE;
}
