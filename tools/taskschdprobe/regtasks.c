/*
 * What a registered task looks like from outside: for each task in \Microsoft\Office, the path and name, the
 * XML IRegisteredTask::get_Xml gives back, what its definition says the URI is, and the first bytes and the size
 * of the file the Task Scheduler keeps it in under %SystemRoot%\System32\Tasks.  Account SIDs, the user name and
 * the computer name are printed as placeholders.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <taskschd.h>
#include <stdio.h>

static WCHAR user[256], computer[256];

/* the text with the SIDs of accounts, the user and the computer left out */
static void print_redacted(const WCHAR *text)
{
    const WCHAR *p = text;

    while (*p)
    {
        if (!wcsncmp(p, L"S-1-5-21-", 9))
        {
            fputws(L"S-1-5-21-<account>", stdout);
            p += 9;
            while (iswdigit(*p) || *p == '-') p++;
        }
        else if (*user && !_wcsnicmp(p, user, wcslen(user)))
        {
            fputws(L"<user>", stdout);
            p += wcslen(user);
        }
        else if (*computer && !_wcsnicmp(p, computer, wcslen(computer)))
        {
            fputws(L"<computer>", stdout);
            p += wcslen(computer);
        }
        else if (*p == '\r')
        {
            fputws(L"\\r", stdout);
            p++;
        }
        else fputwc(*p++, stdout);
    }
}

static void dump_file(const WCHAR *path)
{
    WCHAR name[MAX_PATH];
    unsigned char head[8];
    DWORD size, read = 0, i;
    HANDLE file;

    GetSystemDirectoryW(name, ARRAY_SIZE(name));
    wcscat(name, L"\\Tasks");
    wcscat(name, path);
    file = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        printf("  file: error %lu\n", GetLastError());
        return;
    }
    size = GetFileSize(file, NULL);
    ReadFile(file, head, sizeof(head), &read, NULL);
    CloseHandle(file);
    printf("  file: %lu bytes, starts", size);
    for (i = 0; i < read; ++i) printf(" %02x", head[i]);
    printf("\n");
}

int main(void)
{
    IRegisteredTaskCollection *tasks;
    IRegistrationInfo *info;
    ITaskDefinition *definition;
    IRegisteredTask *task;
    ITaskService *service;
    ITaskFolder *folder;
    DWORD len;
    VARIANT empty, index;
    LONG count, i;
    BSTR text;
    HRESULT hr;

    len = ARRAY_SIZE(user);
    GetUserNameW(user, &len);
    len = ARRAY_SIZE(computer);
    GetComputerNameW(computer, &len);

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskService,
            (void **)&service)))
    {
        printf("CoCreateInstance %#lx\n", hr);
        return 1;
    }
    V_VT(&empty) = VT_EMPTY;
    if (FAILED(hr = ITaskService_Connect(service, empty, empty, empty, empty)))
    {
        printf("Connect %#lx\n", hr);
        return 1;
    }
    text = SysAllocString(L"\\Microsoft\\Office");
    hr = ITaskService_GetFolder(service, text, &folder);
    SysFreeString(text);
    if (FAILED(hr))
    {
        printf("GetFolder %#lx\n", hr);
        return 1;
    }
    if (FAILED(hr = ITaskFolder_GetTasks(folder, TASK_ENUM_HIDDEN, &tasks)))
    {
        printf("GetTasks %#lx\n", hr);
        return 1;
    }
    IRegisteredTaskCollection_get_Count(tasks, &count);
    printf("%ld tasks\n", count);
    for (i = 1; i <= count; ++i)
    {
        V_VT(&index) = VT_I4;
        V_I4(&index) = i;
        if (FAILED(hr = IRegisteredTaskCollection_get_Item(tasks, index, &task)))
        {
            printf("get_Item %ld: %#lx\n", i, hr);
            continue;
        }
        text = NULL;
        IRegisteredTask_get_Path(task, &text);
        printf("== %ls\n", text);
        dump_file(text);
        SysFreeString(text);

        if (SUCCEEDED(hr = IRegisteredTask_get_Definition(task, &definition)))
        {
            ITaskDefinition_get_RegistrationInfo(definition, &info);
            text = NULL;
            hr = IRegistrationInfo_get_URI(info, &text);
            printf("  definition URI: hr %#lx %ls\n", hr, text ? text : L"(null)");
            SysFreeString(text);
            IRegistrationInfo_Release(info);
            ITaskDefinition_Release(definition);
        }
        else printf("  get_Definition %#lx\n", hr);

        text = NULL;
        hr = IRegisteredTask_get_Xml(task, &text);
        printf("  get_Xml: hr %#lx, %u characters\n", hr, text ? SysStringLen(text) : 0);
        if (text)
        {
            print_redacted(text);
            printf("\n  -- end\n");
        }
        SysFreeString(text);
        IRegisteredTask_Release(task);
    }

    IRegisteredTaskCollection_Release(tasks);
    ITaskFolder_Release(folder);
    ITaskService_Release(service);
    CoUninitialize();
    printf("done\n");
    return 0;
}
