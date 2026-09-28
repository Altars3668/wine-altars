/*
 * The rest of a task definition's surface, as Windows answers it: the registration info's security descriptor,
 * the XmlText of the registration info, the settings and the actions on their own, actions of every type and
 * their XML both ways, the enumerators of each collection, and the objects driven through IDispatch as a script
 * would.  Nothing is registered.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <taskschd.h>
#include <sddl.h>
#include <stdio.h>

static const GUID null_guid;

static BSTR bstr(const WCHAR *s)
{
    static BSTR strings[64];
    static unsigned int next;

    SysFreeString(strings[next % 64]);
    return strings[next++ % 64] = SysAllocString(s);
}

static void print_variant(const char *what, HRESULT hr, VARIANT *v)
{
    printf("  %s: hr %#lx vt %d", what, hr, V_VT(v));
    if (V_VT(v) == VT_BSTR && V_BSTR(v) && SysStringLen(V_BSTR(v)) && V_BSTR(v)[0] < 0x20)
    {
        /* bytes, not text */
        UINT i, len = SysStringByteLen(V_BSTR(v));

        printf(" %u bytes:", len);
        for (i = 0; i < len && i < 16; ++i) printf(" %02x", ((const BYTE *)V_BSTR(v))[i]);
    }
    else if (V_VT(v) == VT_BSTR) printf(" \"%ls\"", V_BSTR(v) ? V_BSTR(v) : L"(null)");
    else if (V_VT(v) == VT_I4) printf(" %ld", V_I4(v));
    else if (V_VT(v) == VT_BOOL) printf(" %d", V_BOOL(v));
    else if (V_VT(v) == (VT_ARRAY | VT_UI1)) printf(" %lu bytes", V_ARRAY(v)->rgsabound[0].cElements);
    printf("\n");
}

static void print_bstr(const char *what, HRESULT hr, BSTR value)
{
    if (FAILED(hr)) printf("  %s: hr %#lx\n", what, hr);
    else printf("  %s: hr %#lx \"%ls\"\n", what, hr, value ? value : L"(null)");
    SysFreeString(value);
}

static ITaskDefinition *new_task(ITaskService *service)
{
    ITaskDefinition *definition = NULL;
    HRESULT hr;

    if (FAILED(hr = ITaskService_NewTask(service, 0, &definition))) printf("NewTask %#lx\n", hr);
    return definition;
}

static void print_task_xml(ITaskDefinition *definition)
{
    BSTR xml = NULL;
    HRESULT hr = ITaskDefinition_get_XmlText(definition, &xml);

    printf("  task xml: hr %#lx\n%ls\n", hr, xml ? xml : L"(null)");
    SysFreeString(xml);
}

static void security_descriptor(ITaskService *service)
{
    static const WCHAR task_xml[] =
        L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><RegistrationInfo>"
        "<SecurityDescriptor>%s</SecurityDescriptor></RegistrationInfo>"
        "<Actions><Exec><Command>a</Command></Exec></Actions></Task>";
    static const WCHAR *strings[] = { L"D:(A;;FA;;;BA)(A;;FRFX;;;AU)", L"", L"garbage" };
    PSECURITY_DESCRIPTOR sd;
    ITaskDefinition *definition;
    IRegistrationInfo *info;
    SAFEARRAY *array;
    WCHAR xml[512];
    ULONG size;
    VARIANT v;
    HRESULT hr;
    unsigned int i;
    void *data;

    printf("== security descriptor\n");
    if (!(definition = new_task(service))) return;
    ITaskDefinition_get_RegistrationInfo(definition, &info);

    VariantInit(&v);
    hr = IRegistrationInfo_get_SecurityDescriptor(info, &v);
    print_variant("new", hr, &v);
    VariantClear(&v);

    for (i = 0; i < ARRAY_SIZE(strings); ++i)
    {
        V_VT(&v) = VT_BSTR;
        V_BSTR(&v) = bstr(strings[i]);
        hr = IRegistrationInfo_put_SecurityDescriptor(info, v);
        printf(" put \"%ls\": hr %#lx\n", strings[i], hr);
        VariantInit(&v);
        hr = IRegistrationInfo_get_SecurityDescriptor(info, &v);
        print_variant("get", hr, &v);
        VariantClear(&v);
    }
    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = bstr(strings[0]);
    IRegistrationInfo_put_SecurityDescriptor(info, v);
    print_task_xml(definition);

    IRegistrationInfo_put_Source(info, bstr(L"source"));
    IRegistrationInfo_put_Date(info, bstr(L"2020-01-02T03:04:05"));
    IRegistrationInfo_put_Author(info, bstr(L"author"));
    IRegistrationInfo_put_Version(info, bstr(L"1.0"));
    IRegistrationInfo_put_Description(info, bstr(L"description"));
    IRegistrationInfo_put_Documentation(info, bstr(L"documentation"));
    IRegistrationInfo_put_URI(info, bstr(L"\\uri"));
    print_task_xml(definition);
    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = bstr(L"");
    IRegistrationInfo_put_SecurityDescriptor(info, v);
    print_task_xml(definition);

    V_VT(&v) = VT_I4;
    V_I4(&v) = 5;
    hr = IRegistrationInfo_put_SecurityDescriptor(info, v);
    printf(" put VT_I4: hr %#lx\n", hr);
    VariantInit(&v);
    hr = IRegistrationInfo_get_SecurityDescriptor(info, &v);
    print_variant("get", hr, &v);
    VariantClear(&v);

    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(strings[0], SDDL_REVISION_1, &sd, &size))
    {
        array = SafeArrayCreateVector(VT_UI1, 0, size);
        SafeArrayAccessData(array, &data);
        memcpy(data, sd, size);
        SafeArrayUnaccessData(array);
        LocalFree(sd);
        V_VT(&v) = VT_ARRAY | VT_UI1;
        V_ARRAY(&v) = array;
        hr = IRegistrationInfo_put_SecurityDescriptor(info, v);
        printf(" put binary (%lu bytes): hr %#lx\n", size, hr);
        VariantClear(&v);
        VariantInit(&v);
        hr = IRegistrationInfo_get_SecurityDescriptor(info, &v);
        print_variant("get", hr, &v);
        VariantClear(&v);
    }

    V_VT(&v) = VT_EMPTY;
    hr = IRegistrationInfo_put_SecurityDescriptor(info, v);
    printf(" put VT_EMPTY: hr %#lx\n", hr);
    VariantInit(&v);
    hr = IRegistrationInfo_get_SecurityDescriptor(info, &v);
    print_variant("get", hr, &v);
    VariantClear(&v);

    V_VT(&v) = VT_NULL;
    hr = IRegistrationInfo_put_SecurityDescriptor(info, v);
    printf(" put VT_NULL: hr %#lx\n", hr);
    VariantInit(&v);
    hr = IRegistrationInfo_get_SecurityDescriptor(info, &v);
    print_variant("get", hr, &v);
    VariantClear(&v);
    print_task_xml(definition);
    IRegistrationInfo_Release(info);
    ITaskDefinition_Release(definition);

    for (i = 0; i < ARRAY_SIZE(strings); ++i)
    {
        if (!(definition = new_task(service))) return;
        swprintf(xml, ARRAY_SIZE(xml), task_xml, strings[i]);
        hr = ITaskDefinition_put_XmlText(definition, bstr(xml));
        printf(" read \"%ls\": hr %#lx\n", strings[i], hr);
        ITaskDefinition_get_RegistrationInfo(definition, &info);
        VariantInit(&v);
        hr = IRegistrationInfo_get_SecurityDescriptor(info, &v);
        print_variant("get", hr, &v);
        VariantClear(&v);
        IRegistrationInfo_Release(info);
        ITaskDefinition_Release(definition);
    }
}

static void part_xml(ITaskService *service)
{
    static const WCHAR *reginfo_xml[] =
    {
        L"<RegistrationInfo xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Author>part</Author></RegistrationInfo>",
        L"<RegistrationInfo><Author>part</Author></RegistrationInfo>",
        L"<Author>part</Author>",
    };
    static const WCHAR settings_xml[] =
        L"<Settings xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Priority>4</Priority></Settings>";
    static const WCHAR actions_xml[] =
        L"<Actions xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Exec><Command>x</Command></Exec>"
        "<Exec><Command>y</Command></Exec></Actions>";
    ITaskDefinition *definition;
    IRegistrationInfo *info;
    IActionCollection *actions;
    ITaskSettings *settings;
    IExecAction *exec;
    IAction *action;
    unsigned int i;
    BSTR text;
    LONG count;
    INT priority;
    HRESULT hr;

    printf("== part xml\n");
    if (!(definition = new_task(service))) return;
    ITaskDefinition_get_RegistrationInfo(definition, &info);
    text = NULL;
    hr = IRegistrationInfo_get_XmlText(info, &text);
    print_bstr("new registration info", hr, text);
    IRegistrationInfo_put_Author(info, bstr(L"Wine"));
    IRegistrationInfo_put_Description(info, bstr(L"a & b"));
    IRegistrationInfo_put_Date(info, bstr(L"2020-01-02T03:04:05"));
    text = NULL;
    hr = IRegistrationInfo_get_XmlText(info, &text);
    print_bstr("registration info", hr, text);
    for (i = 0; i < ARRAY_SIZE(reginfo_xml); ++i)
    {
        hr = IRegistrationInfo_put_XmlText(info, bstr(reginfo_xml[i]));
        printf(" put registration info %u: hr %#lx\n", i, hr);
        text = NULL;
        hr = IRegistrationInfo_get_Author(info, &text);
        print_bstr("author", hr, text);
        text = NULL;
        hr = IRegistrationInfo_get_Description(info, &text);
        print_bstr("description", hr, text);
    }
    IRegistrationInfo_Release(info);

    ITaskDefinition_get_Settings(definition, &settings);
    text = NULL;
    hr = ITaskSettings_get_XmlText(settings, &text);
    print_bstr("settings", hr, text);
    hr = ITaskSettings_put_XmlText(settings, bstr(settings_xml));
    printf(" put settings: hr %#lx\n", hr);
    priority = 77;
    ITaskSettings_get_Priority(settings, &priority);
    printf("  priority %d\n", priority);
    ITaskSettings_Release(settings);

    ITaskDefinition_get_Actions(definition, &actions);
    IActionCollection_Create(actions, TASK_ACTION_EXEC, &action);
    IAction_QueryInterface(action, &IID_IExecAction, (void **)&exec);
    IExecAction_put_Path(exec, bstr(L"a.exe"));
    IExecAction_Release(exec);
    IAction_Release(action);
    text = NULL;
    hr = IActionCollection_get_XmlText(actions, &text);
    print_bstr("actions", hr, text);
    hr = IActionCollection_put_XmlText(actions, bstr(actions_xml));
    printf(" put actions: hr %#lx\n", hr);
    count = 77;
    IActionCollection_get_Count(actions, &count);
    printf("  count %ld\n", count);
    IActionCollection_Release(actions);
    ITaskDefinition_Release(definition);
}

static void dump_action(IAction *action)
{
    IComHandlerAction *com;
    IShowMessageAction *message;
    IEmailAction *email;
    IExecAction *exec;
    TASK_ACTION_TYPE type = 77;
    BSTR text;
    HRESULT hr;

    hr = IAction_get_Type(action, &type);
    text = NULL;
    IAction_get_Id(action, &text);
    printf("  action type %d (hr %#lx), id %ls\n", type, hr, text ? text : L"(null)");
    SysFreeString(text);
    if (SUCCEEDED(IAction_QueryInterface(action, &IID_IExecAction, (void **)&exec)))
    {
        text = NULL; hr = IExecAction_get_Path(exec, &text); print_bstr("path", hr, text);
        IExecAction_Release(exec);
    }
    if (SUCCEEDED(IAction_QueryInterface(action, &IID_IComHandlerAction, (void **)&com)))
    {
        text = NULL; hr = IComHandlerAction_get_ClassId(com, &text); print_bstr("class id", hr, text);
        text = NULL; hr = IComHandlerAction_get_Data(com, &text); print_bstr("data", hr, text);
        IComHandlerAction_Release(com);
    }
    if (SUCCEEDED(IAction_QueryInterface(action, &IID_IEmailAction, (void **)&email)))
    {
        ITaskNamedValueCollection *fields = NULL;
        LONG count = -1;

        text = NULL; hr = IEmailAction_get_Server(email, &text); print_bstr("server", hr, text);
        text = NULL; hr = IEmailAction_get_Subject(email, &text); print_bstr("subject", hr, text);
        text = NULL; hr = IEmailAction_get_To(email, &text); print_bstr("to", hr, text);
        text = NULL; hr = IEmailAction_get_Cc(email, &text); print_bstr("cc", hr, text);
        text = NULL; hr = IEmailAction_get_Bcc(email, &text); print_bstr("bcc", hr, text);
        text = NULL; hr = IEmailAction_get_ReplyTo(email, &text); print_bstr("reply to", hr, text);
        text = NULL; hr = IEmailAction_get_From(email, &text); print_bstr("from", hr, text);
        text = NULL; hr = IEmailAction_get_Body(email, &text); print_bstr("body", hr, text);
        hr = IEmailAction_get_HeaderFields(email, &fields);
        if (fields) ITaskNamedValueCollection_get_Count(fields, &count);
        printf("  header fields: hr %#lx count %ld\n", hr, count);
        if (fields) ITaskNamedValueCollection_Release(fields);
        {
            SAFEARRAY *array = NULL;
            VARTYPE vt = 0;

            hr = IEmailAction_get_Attachments(email, &array);
            printf("  attachments: hr %#lx", hr);
            if (array)
            {
                SafeArrayGetVartype(array, &vt);
                printf(" vt %d dims %u, %lu elements", vt, SafeArrayGetDim(array), array->rgsabound[0].cElements);
                if (vt == VT_VARIANT && array->rgsabound[0].cElements)
                {
                    VARIANT item;
                    LONG first = array->rgsabound[0].lLbound;

                    VariantInit(&item);
                    SafeArrayGetElement(array, &first, &item);
                    printf(", first vt %d %ls", V_VT(&item), V_VT(&item) == VT_BSTR ? V_BSTR(&item) : L"");
                    VariantClear(&item);
                }
                SafeArrayDestroy(array);
            }
            printf("\n");
        }
        IEmailAction_Release(email);
    }
    if (SUCCEEDED(IAction_QueryInterface(action, &IID_IShowMessageAction, (void **)&message)))
    {
        text = NULL; hr = IShowMessageAction_get_Title(message, &text); print_bstr("title", hr, text);
        text = NULL; hr = IShowMessageAction_get_MessageBody(message, &text); print_bstr("message", hr, text);
        IShowMessageAction_Release(message);
    }
}

static void action_types(ITaskService *service)
{
    static const TASK_ACTION_TYPE types[] =
        { TASK_ACTION_EXEC, TASK_ACTION_COM_HANDLER, TASK_ACTION_SEND_EMAIL, TASK_ACTION_SHOW_MESSAGE, 1, 8 };
    ITaskDefinition *definition, *reread;
    IActionCollection *actions;
    IComHandlerAction *com;
    IShowMessageAction *message;
    IEmailAction *email;
    IAction *action;
    unsigned int i;
    LONG count, j;
    BSTR xml;
    HRESULT hr;

    printf("== action types\n");
    if (!(definition = new_task(service))) return;
    ITaskDefinition_get_Actions(definition, &actions);
    for (i = 0; i < ARRAY_SIZE(types); ++i)
    {
        action = NULL;
        hr = IActionCollection_Create(actions, types[i], &action);
        printf(" Create %d: hr %#lx\n", types[i], hr);
        if (FAILED(hr)) continue;
        dump_action(action);
        if (types[i] == TASK_ACTION_EXEC)
        {
            IExecAction *exec;

            IAction_QueryInterface(action, &IID_IExecAction, (void **)&exec);
            IExecAction_put_Path(exec, bstr(L"a.exe"));
            IExecAction_Release(exec);
        }
        if (SUCCEEDED(IAction_QueryInterface(action, &IID_IComHandlerAction, (void **)&com)))
        {
            hr = IComHandlerAction_put_ClassId(com, bstr(L"{01234567-89ab-cdef-0123-456789abcdef}"));
            printf("  put_ClassId %#lx\n", hr);
            hr = IComHandlerAction_put_Data(com, bstr(L"data <&> here"));
            printf("  put_Data %#lx\n", hr);
            IComHandlerAction_Release(com);
        }
        if (SUCCEEDED(IAction_QueryInterface(action, &IID_IEmailAction, (void **)&email)))
        {
            ITaskNamedValueCollection *fields;
            ITaskNamedValuePair *pair;
            SAFEARRAY *array;
            VARIANT v;
            LONG index;

            IEmailAction_put_Server(email, bstr(L"smtp.example.com"));
            IEmailAction_put_Subject(email, bstr(L"subject"));
            IEmailAction_put_To(email, bstr(L"to@example.com"));
            IEmailAction_put_Cc(email, bstr(L"cc@example.com"));
            IEmailAction_put_Bcc(email, bstr(L"bcc@example.com"));
            IEmailAction_put_ReplyTo(email, bstr(L"reply@example.com"));
            IEmailAction_put_From(email, bstr(L"from@example.com"));
            IEmailAction_put_Body(email, bstr(L"body"));
            if (SUCCEEDED(hr = IEmailAction_get_HeaderFields(email, &fields)) && fields)
            {
                hr = ITaskNamedValueCollection_Create(fields, bstr(L"X-Name"), bstr(L"value"), &pair);
                printf("  header Create %#lx\n", hr);
                if (SUCCEEDED(hr)) ITaskNamedValuePair_Release(pair);
                ITaskNamedValueCollection_Release(fields);
            }
            else printf("  get_HeaderFields %#lx\n", hr);
            array = SafeArrayCreateVector(VT_VARIANT, 0, 2);
            for (index = 0; index < 2; ++index)
            {
                V_VT(&v) = VT_BSTR;
                V_BSTR(&v) = SysAllocString(index ? L"C:\\b.txt" : L"C:\\a.txt");
                SafeArrayPutElement(array, &index, &v);
                VariantClear(&v);
            }
            hr = IEmailAction_put_Attachments(email, array);
            printf("  put_Attachments %#lx\n", hr);
            SafeArrayDestroy(array);
            IEmailAction_Release(email);
        }
        if (SUCCEEDED(IAction_QueryInterface(action, &IID_IShowMessageAction, (void **)&message)))
        {
            IShowMessageAction_put_Title(message, bstr(L"title"));
            IShowMessageAction_put_MessageBody(message, bstr(L"message"));
            IShowMessageAction_Release(message);
        }
        IAction_Release(action);
    }
    IActionCollection_Release(actions);

    xml = NULL;
    hr = ITaskDefinition_get_XmlText(definition, &xml);
    printf(" xml: hr %#lx\n%ls\n", hr, xml ? xml : L"(null)");
    if (xml && (reread = new_task(service)))
    {
        hr = ITaskDefinition_put_XmlText(reread, xml);
        printf(" read back: hr %#lx\n", hr);
        ITaskDefinition_get_Actions(reread, &actions);
        count = 0;
        IActionCollection_get_Count(actions, &count);
        for (j = 1; j <= count; ++j)
        {
            if (SUCCEEDED(IActionCollection_get_Item(actions, j, &action)))
            {
                dump_action(action);
                IAction_Release(action);
            }
        }
        IActionCollection_Release(actions);
        ITaskDefinition_Release(reread);
    }
    SysFreeString(xml);
    ITaskDefinition_Release(definition);
}

static void walk(const char *what, IUnknown *collection_enum)
{
    IEnumVARIANT *enumvar, *clone;
    VARIANT v[4];
    ULONG fetched, i;
    HRESULT hr;

    hr = IUnknown_QueryInterface(collection_enum, &IID_IEnumVARIANT, (void **)&enumvar);
    printf(" %s: IEnumVARIANT hr %#lx\n", what, hr);
    if (FAILED(hr)) return;
    for (;;)
    {
        VariantInit(&v[0]);
        fetched = 77;
        hr = IEnumVARIANT_Next(enumvar, 1, v, &fetched);
        printf("  Next(1): hr %#lx fetched %lu vt %d\n", hr, fetched, V_VT(&v[0]));
        VariantClear(&v[0]);
        if (hr != S_OK) break;
    }
    hr = IEnumVARIANT_Reset(enumvar);
    printf("  Reset: hr %#lx\n", hr);
    for (i = 0; i < 4; ++i) VariantInit(&v[i]);
    fetched = 77;
    hr = IEnumVARIANT_Next(enumvar, 4, v, &fetched);
    printf("  Next(4): hr %#lx fetched %lu\n", hr, fetched);
    for (i = 0; i < 4; ++i) VariantClear(&v[i]);
    hr = IEnumVARIANT_Reset(enumvar);
    hr = IEnumVARIANT_Skip(enumvar, 1);
    printf("  Skip(1): hr %#lx\n", hr);
    hr = IEnumVARIANT_Skip(enumvar, 5);
    printf("  Skip(5): hr %#lx\n", hr);
    hr = IEnumVARIANT_Reset(enumvar);
    clone = NULL;
    hr = IEnumVARIANT_Clone(enumvar, &clone);
    printf("  Clone: hr %#lx\n", hr);
    if (clone)
    {
        VariantInit(&v[0]);
        fetched = 77;
        hr = IEnumVARIANT_Next(clone, 1, v, &fetched);
        printf("  clone Next(1): hr %#lx fetched %lu vt %d\n", hr, fetched, V_VT(&v[0]));
        VariantClear(&v[0]);
        IEnumVARIANT_Release(clone);
    }
    fetched = 77;
    hr = IEnumVARIANT_Next(enumvar, 1, v, NULL);
    printf("  Next(1, NULL): hr %#lx\n", hr);
    VariantClear(&v[0]);
    IEnumVARIANT_Release(enumvar);
}

static void enumerators(ITaskService *service)
{
    ITaskNamedValueCollection *values;
    ITriggerCollection *triggers;
    IActionCollection *actions;
    ITaskDefinition *definition;
    IEventTrigger *event;
    ITaskNamedValuePair *pair;
    ITaskFolderCollection *folders;
    ITaskFolder *root;
    ITrigger *trigger;
    IAction *action;
    IUnknown *unk;
    unsigned int i;
    HRESULT hr;

    printf("== enumerators\n");
    if (!(definition = new_task(service))) return;
    ITaskDefinition_get_Triggers(definition, &triggers);
    hr = ITriggerCollection_get__NewEnum(triggers, &unk);
    printf(" empty triggers _NewEnum: hr %#lx\n", hr);
    if (SUCCEEDED(hr)) { walk("empty triggers", unk); IUnknown_Release(unk); }
    for (i = 0; i < 3; ++i)
    {
        ITriggerCollection_Create(triggers, i ? TASK_TRIGGER_BOOT : TASK_TRIGGER_EVENT, &trigger);
        ITrigger_Release(trigger);
    }
    hr = ITriggerCollection_get__NewEnum(triggers, &unk);
    printf(" triggers _NewEnum: hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        walk("triggers", unk);
        IUnknown_Release(unk);
    }

    ITriggerCollection_get_Item(triggers, 1, &trigger);
    if (SUCCEEDED(ITrigger_QueryInterface(trigger, &IID_IEventTrigger, (void **)&event)))
    {
        hr = IEventTrigger_get_ValueQueries(event, &values);
        printf(" value queries: hr %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            for (i = 0; i < 2; ++i)
            {
                ITaskNamedValueCollection_Create(values, bstr(i ? L"b" : L"a"), bstr(L"v"), &pair);
                ITaskNamedValuePair_Release(pair);
            }
            hr = ITaskNamedValueCollection_get__NewEnum(values, &unk);
            printf(" values _NewEnum: hr %#lx\n", hr);
            if (SUCCEEDED(hr)) { walk("values", unk); IUnknown_Release(unk); }
            ITaskNamedValueCollection_Release(values);
        }
        IEventTrigger_Release(event);
    }
    else printf(" first trigger is not an event trigger\n");
    ITrigger_Release(trigger);
    ITriggerCollection_Release(triggers);

    ITaskDefinition_get_Actions(definition, &actions);
    for (i = 0; i < 2; ++i)
    {
        IActionCollection_Create(actions, TASK_ACTION_EXEC, &action);
        IAction_Release(action);
    }
    hr = IActionCollection_get__NewEnum(actions, &unk);
    printf(" actions _NewEnum: hr %#lx\n", hr);
    if (SUCCEEDED(hr)) { walk("actions", unk); IUnknown_Release(unk); }
    IActionCollection_Release(actions);
    ITaskDefinition_Release(definition);

    if (SUCCEEDED(hr = ITaskService_GetFolder(service, bstr(L"\\"), &root)))
    {
        if (SUCCEEDED(hr = ITaskFolder_GetFolders(root, 0, &folders)))
        {
            hr = ITaskFolderCollection_get__NewEnum(folders, &unk);
            printf(" folders _NewEnum: hr %#lx\n", hr);
            if (SUCCEEDED(hr))
            {
                IEnumVARIANT *enumvar;
                VARIANT v;
                ULONG fetched;

                LONG count = 0;

                /* whatever folders there are here, the enumerator gives the first of them */
                ITaskFolderCollection_get_Count(folders, &count);
                IUnknown_QueryInterface(unk, &IID_IEnumVARIANT, (void **)&enumvar);
                VariantInit(&v);
                hr = IEnumVARIANT_Next(enumvar, 1, &v, &fetched);
                printf("  folders Next(1): as many as there are %d, hr %s, vt %s\n", fetched == (count > 0),
                        hr == (count ? S_OK : S_FALSE) ? "as expected" : "unexpected",
                        V_VT(&v) == (count ? VT_DISPATCH : VT_EMPTY) ? "as expected" : "unexpected");
                VariantClear(&v);
                IEnumVARIANT_Release(enumvar);
                IUnknown_Release(unk);
            }
            ITaskFolderCollection_Release(folders);
        }
        else printf(" GetFolders %#lx\n", hr);
        ITaskFolder_Release(root);
    }
}

/* What Windows 10 added: a hidden window for Exec, the process token's SID type and required privileges of the
 * principal, maintenance settings and volatility -- their defaults, their XML, and the XML read back. */
static void additions(ITaskService *service)
{
    static const WCHAR *reads[] =
    {
        L"<Task version=\"1.6\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Principals><Principal>"
        "<ProcessTokenSidType>Unrestricted</ProcessTokenSidType><RequiredPrivileges><Privilege>SeBackupPrivilege</Privilege>"
        "<Privilege>SeRestorePrivilege</Privilege></RequiredPrivileges></Principal></Principals><Settings><Volatile>true</Volatile>"
        "<MaintenanceSettings><Period>P2D</Period><Deadline>P14D</Deadline><Exclusive>true</Exclusive></MaintenanceSettings>"
        "</Settings><Actions><Exec><Command>a</Command><HideAppWindow>true</HideAppWindow></Exec></Actions></Task>",
        L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Principals><Principal>"
        "<ProcessTokenSidType>Bogus</ProcessTokenSidType></Principal></Principals><Actions><Exec><Command>a</Command></Exec></Actions></Task>",
        L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Settings><MaintenanceSettings><Period>P1D</Period>"
        "</MaintenanceSettings></Settings><Actions><Exec><Command>a</Command></Exec></Actions></Task>",
        L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Settings><MaintenanceSettings><Deadline>P1D</Deadline>"
        "</MaintenanceSettings></Settings><Actions><Exec><Command>a</Command></Exec></Actions></Task>",
        L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Principals><Principal>"
        "<RequiredPrivileges><Privilege>SeBogusPrivilege</Privilege></RequiredPrivileges></Principal></Principals>"
        "<Actions><Exec><Command>a</Command></Exec></Actions></Task>",
        L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Settings><Volatile>false</Volatile></Settings>"
        "<Actions><Exec><Command>a</Command><HideAppWindow>false</HideAppWindow></Exec></Actions></Task>",
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Settings><Volatile>true</Volatile></Settings>"
        "<Actions><Exec><Command>a</Command></Exec></Actions></Task>",
    };
    ITaskDefinition *definition, *reread;
    IMaintenanceSettings *maintenance, *other;
    IActionCollection *actions;
    ITaskSettings3 *settings3;
    ITaskSettings *settings;
    IPrincipal2 *principal2;
    IPrincipal *principal;
    IExecAction2 *exec2;
    IAction *action;
    TASK_PROCESSTOKENSID_TYPE sid_type;
    VARIANT_BOOL b;
    unsigned int i;
    LONG count;
    BSTR text;
    HRESULT hr;

    printf("== additions\n");
    if (!(definition = new_task(service))) return;

    ITaskDefinition_get_Actions(definition, &actions);
    IActionCollection_Create(actions, TASK_ACTION_EXEC, &action);
    hr = IAction_QueryInterface(action, &IID_IExecAction2, (void **)&exec2);
    printf(" IExecAction2: hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        b = 7;
        hr = IExecAction2_get_HideAppWindow(exec2, &b);
        printf("  HideAppWindow: hr %#lx %d\n", hr, b);
        IExecAction2_put_Path(exec2, bstr(L"a.exe"));
        hr = IExecAction2_put_HideAppWindow(exec2, VARIANT_TRUE);
        printf("  put_HideAppWindow: hr %#lx\n", hr);
        IExecAction2_Release(exec2);
    }
    IAction_Release(action);
    IActionCollection_Release(actions);

    ITaskDefinition_get_Principal(definition, &principal);
    hr = IPrincipal_QueryInterface(principal, &IID_IPrincipal2, (void **)&principal2);
    printf(" IPrincipal2: hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        sid_type = 77;
        hr = IPrincipal2_get_ProcessTokenSidType(principal2, &sid_type);
        printf("  ProcessTokenSidType: hr %#lx %d\n", hr, sid_type);
        count = 77;
        hr = IPrincipal2_get_RequiredPrivilegeCount(principal2, &count);
        printf("  RequiredPrivilegeCount: hr %#lx %ld\n", hr, count);
        text = NULL;
        hr = IPrincipal2_get_RequiredPrivilege(principal2, 0, &text);
        print_bstr("RequiredPrivilege(0)", hr, text);
        text = NULL;
        hr = IPrincipal2_get_RequiredPrivilege(principal2, 1, &text);
        print_bstr("RequiredPrivilege(1)", hr, text);
        hr = IPrincipal2_put_ProcessTokenSidType(principal2, 3);
        printf("  put_ProcessTokenSidType(3): hr %#lx\n", hr);
        hr = IPrincipal2_put_ProcessTokenSidType(principal2, TASK_PROCESSTOKENSID_UNRESTRICTED);
        printf("  put_ProcessTokenSidType(unrestricted): hr %#lx\n", hr);
        hr = IPrincipal2_AddRequiredPrivilege(principal2, bstr(L"SeBackupPrivilege"));
        printf("  AddRequiredPrivilege: hr %#lx\n", hr);
        hr = IPrincipal2_AddRequiredPrivilege(principal2, bstr(L"SeRestorePrivilege"));
        printf("  AddRequiredPrivilege: hr %#lx\n", hr);
        hr = IPrincipal2_AddRequiredPrivilege(principal2, bstr(L"SeBackupPrivilege"));
        printf("  AddRequiredPrivilege again: hr %#lx\n", hr);
        hr = IPrincipal2_AddRequiredPrivilege(principal2, bstr(L"SeBogusPrivilege"));
        printf("  AddRequiredPrivilege bogus: hr %#lx\n", hr);
        hr = IPrincipal2_AddRequiredPrivilege(principal2, NULL);
        printf("  AddRequiredPrivilege NULL: hr %#lx\n", hr);
        count = 77;
        IPrincipal2_get_RequiredPrivilegeCount(principal2, &count);
        printf("  RequiredPrivilegeCount: %ld\n", count);
        for (i = 0; i <= count + 1; ++i)
        {
            text = NULL;
            hr = IPrincipal2_get_RequiredPrivilege(principal2, i, &text);
            printf("  RequiredPrivilege(%u): hr %#lx %ls\n", i, hr, text ? text : L"(null)");
            SysFreeString(text);
        }
        IPrincipal2_Release(principal2);
    }
    IPrincipal_Release(principal);

    ITaskDefinition_get_Settings(definition, &settings);
    hr = ITaskSettings_QueryInterface(settings, &IID_ITaskSettings3, (void **)&settings3);
    printf(" ITaskSettings3: hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        b = 7;
        hr = ITaskSettings3_get_Volatile(settings3, &b);
        printf("  Volatile: hr %#lx %d\n", hr, b);
        maintenance = (void *)0xdeadbeef;
        hr = ITaskSettings3_get_MaintenanceSettings(settings3, &maintenance);
        printf("  MaintenanceSettings: hr %#lx %s\n", hr, maintenance ? (maintenance == (void *)0xdeadbeef ? "untouched" : "object") : "NULL");
        if (maintenance && maintenance != (void *)0xdeadbeef) IMaintenanceSettings_Release(maintenance);
        maintenance = NULL;
        hr = ITaskSettings3_CreateMaintenanceSettings(settings3, &maintenance);
        printf("  CreateMaintenanceSettings: hr %#lx %s\n", hr, maintenance ? "object" : "NULL");
        if (maintenance)
        {
            text = NULL;
            hr = IMaintenanceSettings_get_Period(maintenance, &text);
            print_bstr("Period", hr, text);
            text = NULL;
            hr = IMaintenanceSettings_get_Deadline(maintenance, &text);
            print_bstr("Deadline", hr, text);
            b = 7;
            hr = IMaintenanceSettings_get_Exclusive(maintenance, &b);
            printf("  Exclusive: hr %#lx %d\n", hr, b);
            hr = IMaintenanceSettings_put_Period(maintenance, bstr(L"P2D"));
            printf("  put_Period: hr %#lx\n", hr);
            hr = IMaintenanceSettings_put_Deadline(maintenance, bstr(L"P14D"));
            printf("  put_Deadline: hr %#lx\n", hr);
            hr = IMaintenanceSettings_put_Exclusive(maintenance, VARIANT_TRUE);
            printf("  put_Exclusive: hr %#lx\n", hr);
            other = NULL;
            hr = ITaskSettings3_get_MaintenanceSettings(settings3, &other);
            printf("  MaintenanceSettings again: hr %#lx %s\n", hr, other ? (other == maintenance ? "same" : "other") : "NULL");
            if (other) IMaintenanceSettings_Release(other);
            other = NULL;
            hr = ITaskSettings3_CreateMaintenanceSettings(settings3, &other);
            printf("  CreateMaintenanceSettings again: hr %#lx %s\n", hr, other ? (other == maintenance ? "same" : "other") : "NULL");
            if (other)
            {
                text = NULL;
                IMaintenanceSettings_get_Period(other, &text);
                print_bstr("its Period", S_OK, text);
                IMaintenanceSettings_Release(other);
            }
            hr = ITaskSettings3_put_MaintenanceSettings(settings3, maintenance);
            printf("  put_MaintenanceSettings: hr %#lx\n", hr);
            IMaintenanceSettings_Release(maintenance);
        }
        hr = ITaskSettings3_put_Volatile(settings3, VARIANT_TRUE);
        printf("  put_Volatile: hr %#lx\n", hr);
        ITaskSettings3_Release(settings3);
    }
    ITaskSettings_Release(settings);

    print_task_xml(definition);
    ITaskDefinition_get_Settings(definition, &settings);
    ITaskSettings_put_Compatibility(settings, TASK_COMPATIBILITY_V2_4);
    ITaskSettings_Release(settings);
    print_task_xml(definition);

    text = NULL;
    ITaskDefinition_get_XmlText(definition, &text);
    if (text && (reread = new_task(service)))
    {
        hr = ITaskDefinition_put_XmlText(reread, text);
        printf(" read back: hr %#lx\n", hr);
        print_task_xml(reread);
        ITaskDefinition_Release(reread);
    }
    SysFreeString(text);
    ITaskDefinition_Release(definition);

    for (i = 0; i < ARRAY_SIZE(reads); ++i)
    {
        if (!(reread = new_task(service))) return;
        hr = ITaskDefinition_put_XmlText(reread, bstr(reads[i]));
        printf(" read %u: hr %#lx\n", i, hr);
        if (SUCCEEDED(hr)) print_task_xml(reread);
        ITaskDefinition_Release(reread);
    }
}

/* The version each feature asks of a task that has nothing else, and where it goes. */
static void versions(ITaskService *service)
{
    ITaskDefinition *definition;
    IMaintenanceSettings *maintenance;
    IActionCollection *actions;
    ITaskSettings3 *settings3;
    ITaskSettings *settings;
    IPrincipal2 *principal2;
    IPrincipal *principal;
    IExecAction2 *exec2;
    IAction *action;
    unsigned int i;
    BSTR xml;

    printf("== versions\n");
    for (i = 0; i < 18; ++i)
    {
        if (!(definition = new_task(service))) return;
        ITaskDefinition_get_Actions(definition, &actions);
        IActionCollection_Create(actions, TASK_ACTION_EXEC, &action);
        IAction_QueryInterface(action, &IID_IExecAction2, (void **)&exec2);
        IExecAction2_put_Path(exec2, bstr(L"a.exe"));
        IExecAction2_put_WorkingDirectory(exec2, bstr(L"C:\\"));
        ITaskDefinition_get_Settings(definition, &settings);
        ITaskSettings_QueryInterface(settings, &IID_ITaskSettings3, (void **)&settings3);
        ITaskDefinition_get_Principal(definition, &principal);
        IPrincipal_QueryInterface(principal, &IID_IPrincipal2, (void **)&principal2);
        switch (i)
        {
        case 0: printf(" nothing\n"); break;
        case 1: printf(" Volatile true\n"); ITaskSettings3_put_Volatile(settings3, VARIANT_TRUE); break;
        case 2: printf(" Volatile false\n"); ITaskSettings3_put_Volatile(settings3, VARIANT_FALSE); break;
        case 3: printf(" HideAppWindow true\n"); IExecAction2_put_HideAppWindow(exec2, VARIANT_TRUE); break;
        case 4: printf(" HideAppWindow false\n"); IExecAction2_put_HideAppWindow(exec2, VARIANT_FALSE); break;
        case 5:
            printf(" maintenance\n");
            ITaskSettings3_CreateMaintenanceSettings(settings3, &maintenance);
            IMaintenanceSettings_put_Period(maintenance, bstr(L"P1D"));
            IMaintenanceSettings_Release(maintenance);
            break;
        case 6: printf(" DisallowStartOnRemoteAppSession true\n"); ITaskSettings3_put_DisallowStartOnRemoteAppSession(settings3, VARIANT_TRUE); break;
        case 7: printf(" UseUnifiedSchedulingEngine true\n"); ITaskSettings3_put_UseUnifiedSchedulingEngine(settings3, VARIANT_TRUE); break;
        case 8: printf(" UserId only\n"); IPrincipal_put_UserId(principal, bstr(L"S-1-5-18")); break;
        case 9:
            printf(" UserId, ProcessTokenSidType unrestricted\n");
            IPrincipal_put_UserId(principal, bstr(L"S-1-5-18"));
            IPrincipal2_put_ProcessTokenSidType(principal2, TASK_PROCESSTOKENSID_UNRESTRICTED);
            break;
        case 10:
            printf(" UserId, ProcessTokenSidType default\n");
            IPrincipal_put_UserId(principal, bstr(L"S-1-5-18"));
            IPrincipal2_put_ProcessTokenSidType(principal2, TASK_PROCESSTOKENSID_DEFAULT);
            break;
        case 11:
            printf(" UserId, ProcessTokenSidType none\n");
            IPrincipal_put_UserId(principal, bstr(L"S-1-5-18"));
            IPrincipal2_put_ProcessTokenSidType(principal2, TASK_PROCESSTOKENSID_NONE);
            break;
        case 12:
            printf(" UserId, privileges\n");
            IPrincipal_put_UserId(principal, bstr(L"S-1-5-18"));
            IPrincipal2_AddRequiredPrivilege(principal2, bstr(L"SeBackupPrivilege"));
            IPrincipal2_AddRequiredPrivilege(principal2, bstr(L"SeRestorePrivilege"));
            break;
        case 14: printf(" DisallowStartOnRemoteAppSession false\n"); ITaskSettings3_put_DisallowStartOnRemoteAppSession(settings3, VARIANT_FALSE); break;
        case 15: printf(" UseUnifiedSchedulingEngine false\n"); ITaskSettings3_put_UseUnifiedSchedulingEngine(settings3, VARIANT_FALSE); break;
        case 16:
            printf(" Volatile, then Compatibility V2\n");
            ITaskSettings3_put_Volatile(settings3, VARIANT_TRUE);
            ITaskSettings_put_Compatibility(settings, TASK_COMPATIBILITY_V2);
            break;
        case 17:
            printf(" Compatibility V2_3, then UseUnifiedSchedulingEngine true\n");
            ITaskSettings_put_Compatibility(settings, TASK_COMPATIBILITY_V2_3);
            ITaskSettings3_put_UseUnifiedSchedulingEngine(settings3, VARIANT_TRUE);
            break;
        case 13:
            printf(" GroupId, privileges, unrestricted, all settings\n");
            IPrincipal_put_GroupId(principal, bstr(L"S-1-5-32-545"));
            IPrincipal2_AddRequiredPrivilege(principal2, bstr(L"SeBackupPrivilege"));
            IPrincipal2_put_ProcessTokenSidType(principal2, TASK_PROCESSTOKENSID_UNRESTRICTED);
            ITaskSettings3_put_Volatile(settings3, VARIANT_TRUE);
            ITaskSettings3_CreateMaintenanceSettings(settings3, &maintenance);
            IMaintenanceSettings_put_Period(maintenance, bstr(L"P1D"));
            IMaintenanceSettings_Release(maintenance);
            ITaskSettings_put_DeleteExpiredTaskAfter(settings, bstr(L"PT0S"));
            ITaskSettings_put_RestartCount(settings, 2);
            ITaskSettings_put_RestartInterval(settings, bstr(L"PT1M"));
            IExecAction2_put_Arguments(exec2, bstr(L"x"));
            IExecAction2_put_HideAppWindow(exec2, VARIANT_TRUE);
            break;
        }
        {
            TASK_COMPATIBILITY compat = 77;
            ITaskSettings_get_Compatibility(settings, &compat);
            printf("  Compatibility %d\n", compat);
        }
        xml = NULL;
        ITaskDefinition_get_XmlText(definition, &xml);
        if (i == 13) printf("%ls\n", xml ? xml : L"(null)");
        else if (xml)
        {
            const WCHAR *p = xml, *end;
            while ((end = wcschr(p, '\n')))
            {
                if ((wcsstr(p, L"version=") < end && wcsstr(p, L"version=")) ||
                    (wcsstr(p, L"Volatile") && wcsstr(p, L"Volatile") < end) ||
                    (wcsstr(p, L"HideAppWindow") && wcsstr(p, L"HideAppWindow") < end) ||
                    (wcsstr(p, L"Maintenance") && wcsstr(p, L"Maintenance") < end) ||
                    (wcsstr(p, L"ProcessToken") && wcsstr(p, L"ProcessToken") < end) ||
                    (wcsstr(p, L"Privilege") && wcsstr(p, L"Privilege") < end) ||
                    (wcsstr(p, L"UserId") && wcsstr(p, L"UserId") < end) ||
                    (wcsstr(p, L"RemoteApp") && wcsstr(p, L"RemoteApp") < end) ||
                    (wcsstr(p, L"Unified") && wcsstr(p, L"Unified") < end))
                    printf("  %.*ls\n", (int)(end - p - (end > p && end[-1] == '\r')), p);
                p = end + 1;
            }
        }
        SysFreeString(xml);
        IPrincipal2_Release(principal2);
        IPrincipal_Release(principal);
        ITaskSettings3_Release(settings3);
        ITaskSettings_Release(settings);
        IExecAction2_Release(exec2);
        IAction_Release(action);
        IActionCollection_Release(actions);
        ITaskDefinition_Release(definition);
    }
}

static HRESULT invoke(IDispatch *disp, const WCHAR *name, WORD flags, VARIANT *args, UINT count, VARIANT *result,
        DISPID *id)
{
    DISPID dispid, put = DISPID_PROPERTYPUT;
    DISPPARAMS params = { args, NULL, count, 0 };
    OLECHAR *names = (OLECHAR *)name;
    EXCEPINFO excep;
    UINT arg_err;
    HRESULT hr;

    if (FAILED(hr = IDispatch_GetIDsOfNames(disp, &null_guid, &names, 1, LOCALE_USER_DEFAULT, &dispid)))
    {
        printf("  GetIDsOfNames %ls: hr %#lx\n", name, hr);
        return hr;
    }
    if (id) *id = dispid;
    if (flags & DISPATCH_PROPERTYPUT)
    {
        params.rgdispidNamedArgs = &put;
        params.cNamedArgs = 1;
    }
    memset(&excep, 0, sizeof(excep));
    return IDispatch_Invoke(disp, dispid, &null_guid, LOCALE_USER_DEFAULT, flags, &params, result, &excep, &arg_err);
}

static void dispatch(ITaskService *service)
{
    static const WCHAR *names[] = { L"RegistrationInfo", L"XmlText", L"xmltext", L"Nonexistent" };
    ITaskDefinition *definition;
    ITypeInfo *typeinfo;
    IDispatch *disp, *info, *triggers;
    VARIANT result, arg;
    UINT count;
    DISPID id;
    HRESULT hr;
    unsigned int i;

    printf("== dispatch\n");
    hr = ITaskService_QueryInterface(service, &IID_IDispatch, (void **)&disp);
    count = 77;
    hr = IDispatch_GetTypeInfoCount(disp, &count);
    printf(" service GetTypeInfoCount: hr %#lx count %u\n", hr, count);
    typeinfo = NULL;
    hr = IDispatch_GetTypeInfo(disp, 0, LOCALE_USER_DEFAULT, &typeinfo);
    printf(" service GetTypeInfo: hr %#lx\n", hr);
    if (typeinfo)
    {
        TYPEATTR *attr;
        ITypeInfo_GetTypeAttr(typeinfo, &attr);
        printf("  typeinfo kind %d flags %#x guid is ITaskService %d\n", attr->typekind, attr->wTypeFlags,
                IsEqualGUID(&attr->guid, &IID_ITaskService));
        ITypeInfo_ReleaseTypeAttr(typeinfo, attr);
        ITypeInfo_Release(typeinfo);
    }
    hr = IDispatch_GetTypeInfo(disp, 1, LOCALE_USER_DEFAULT, &typeinfo);
    printf(" service GetTypeInfo(1): hr %#lx\n", hr);
    VariantInit(&result);
    hr = invoke(disp, L"Connected", DISPATCH_PROPERTYGET, NULL, 0, &result, &id);
    printf(" Connected: hr %#lx dispid %ld vt %d value %d\n", hr, id, V_VT(&result), V_BOOL(&result));
    VariantInit(&result);
    hr = invoke(disp, L"HighestVersion", DISPATCH_PROPERTYGET, NULL, 0, &result, &id);
    printf(" HighestVersion: hr %#lx dispid %ld vt %d value %#lx\n", hr, id, V_VT(&result), V_I4(&result));
    VariantInit(&result);
    V_VT(&arg) = VT_I4;
    V_I4(&arg) = 0;
    hr = invoke(disp, L"NewTask", DISPATCH_METHOD, &arg, 1, &result, &id);
    printf(" NewTask: hr %#lx dispid %ld vt %d\n", hr, id, V_VT(&result));
    VariantClear(&result);
    IDispatch_Release(disp);

    if (!(definition = new_task(service))) return;
    ITaskDefinition_QueryInterface(definition, &IID_IDispatch, (void **)&disp);
    for (i = 0; i < ARRAY_SIZE(names); ++i)
    {
        OLECHAR *name = (OLECHAR *)names[i];
        id = 77;
        hr = IDispatch_GetIDsOfNames(disp, &null_guid, &name, 1, LOCALE_USER_DEFAULT, &id);
        printf(" definition GetIDsOfNames %ls: hr %#lx dispid %ld\n", names[i], hr, id);
    }
    VariantInit(&result);
    hr = invoke(disp, L"RegistrationInfo", DISPATCH_PROPERTYGET, NULL, 0, &result, NULL);
    printf(" RegistrationInfo: hr %#lx vt %d\n", hr, V_VT(&result));
    if (V_VT(&result) == VT_DISPATCH)
    {
        info = V_DISPATCH(&result);
        V_VT(&arg) = VT_BSTR;
        V_BSTR(&arg) = bstr(L"through IDispatch");
        hr = invoke(info, L"Author", DISPATCH_PROPERTYPUT, &arg, 1, NULL, NULL);
        printf(" put Author: hr %#lx\n", hr);
        VariantInit(&result);
        hr = invoke(info, L"Author", DISPATCH_PROPERTYGET, NULL, 0, &result, NULL);
        printf(" get Author: hr %#lx vt %d %ls\n", hr, V_VT(&result), V_VT(&result) == VT_BSTR ? V_BSTR(&result) : L"");
        VariantClear(&result);
        IDispatch_Release(info);
    }
    VariantInit(&result);
    hr = invoke(disp, L"Triggers", DISPATCH_PROPERTYGET, NULL, 0, &result, NULL);
    printf(" Triggers: hr %#lx vt %d\n", hr, V_VT(&result));
    if (V_VT(&result) == VT_DISPATCH)
    {
        DISPPARAMS none = { NULL, NULL, 0, 0 };
        triggers = V_DISPATCH(&result);
        V_VT(&arg) = VT_I4;
        V_I4(&arg) = TASK_TRIGGER_TIME;
        VariantInit(&result);
        hr = invoke(triggers, L"Create", DISPATCH_METHOD, &arg, 1, &result, NULL);
        printf(" Create: hr %#lx vt %d\n", hr, V_VT(&result));
        VariantClear(&result);
        VariantInit(&result);
        hr = invoke(triggers, L"Count", DISPATCH_PROPERTYGET, NULL, 0, &result, NULL);
        printf(" Count: hr %#lx vt %d value %ld\n", hr, V_VT(&result), V_I4(&result));
        VariantInit(&result);
        hr = IDispatch_Invoke(triggers, DISPID_NEWENUM, &null_guid, LOCALE_USER_DEFAULT,
                DISPATCH_METHOD | DISPATCH_PROPERTYGET, &none, &result, NULL, NULL);
        printf(" DISPID_NEWENUM: hr %#lx vt %d\n", hr, V_VT(&result));
        VariantClear(&result);
        V_VT(&arg) = VT_I4;
        V_I4(&arg) = 1;
        VariantInit(&result);
        hr = invoke(triggers, L"Item", DISPATCH_PROPERTYGET, &arg, 1, &result, &id);
        printf(" Item(1): hr %#lx dispid %ld vt %d\n", hr, id, V_VT(&result));
        VariantClear(&result);
        IDispatch_Release(triggers);
    }
    VariantInit(&result);
    hr = invoke(disp, L"XmlText", DISPATCH_PROPERTYGET, NULL, 0, &result, NULL);
    printf(" XmlText: hr %#lx vt %d, %u characters\n", hr, V_VT(&result),
            V_VT(&result) == VT_BSTR ? SysStringLen(V_BSTR(&result)) : 0);
    VariantClear(&result);
    IDispatch_Release(disp);
    ITaskDefinition_Release(definition);
}

/* Which interface's type information each object's IDispatch speaks for. */
static void dispatch_types(ITaskService *service)
{
    static const struct { const WCHAR *name; const IID *iid; } ifaces[] =
    {
        { L"ITaskSettings", &IID_ITaskSettings }, { L"ITaskSettings2", &IID_ITaskSettings2 },
        { L"ITaskSettings3", &IID_ITaskSettings3 }, { L"IPrincipal", &IID_IPrincipal },
        { L"IPrincipal2", &IID_IPrincipal2 }, { L"IExecAction", &IID_IExecAction },
        { L"IExecAction2", &IID_IExecAction2 }, { L"IAction", &IID_IAction }, { L"ITrigger", &IID_ITrigger },
        { L"IDailyTrigger", &IID_IDailyTrigger }, { L"IComHandlerAction", &IID_IComHandlerAction },
        { L"IRegistrationInfo", &IID_IRegistrationInfo }, { L"ITaskDefinition", &IID_ITaskDefinition },
    };
    static const WCHAR *names[] = { L"Volatile", L"DisallowStartOnRemoteAppSession", L"HideAppWindow",
                                    L"ProcessTokenSidType", L"DaysInterval", L"Priority", L"Path", L"RunLevel" };
    ITaskDefinition *definition;
    IActionCollection *actions;
    ITriggerCollection *triggers;
    ITaskSettings *settings;
    IPrincipal *principal;
    ITrigger *trigger;
    IAction *action, *com;
    IUnknown *objects[5];
    const char *labels[5] = { "settings", "principal", "exec", "daily trigger", "com handler" };
    unsigned int i, j, k;

    printf("== dispatch types\n");
    if (!(definition = new_task(service))) return;
    ITaskDefinition_get_Settings(definition, &settings);
    ITaskDefinition_get_Principal(definition, &principal);
    ITaskDefinition_get_Actions(definition, &actions);
    IActionCollection_Create(actions, TASK_ACTION_EXEC, &action);
    IActionCollection_Create(actions, TASK_ACTION_COM_HANDLER, &com);
    ITaskDefinition_get_Triggers(definition, &triggers);
    ITriggerCollection_Create(triggers, TASK_TRIGGER_DAILY, &trigger);
    objects[0] = (IUnknown *)settings; objects[1] = (IUnknown *)principal; objects[2] = (IUnknown *)action;
    objects[3] = (IUnknown *)trigger; objects[4] = (IUnknown *)com;
    for (i = 0; i < 5; ++i)
    {
        for (j = 0; j < ARRAY_SIZE(ifaces); ++j)
        {
            IDispatch *disp;
            ITypeInfo *info;
            TYPEATTR *attr;

            if (FAILED(IUnknown_QueryInterface(objects[i], ifaces[j].iid, (void **)&disp))) continue;
            printf(" %s as %ls:", labels[i], ifaces[j].name);
            if (SUCCEEDED(IDispatch_GetTypeInfo(disp, 0, LOCALE_USER_DEFAULT, &info)))
            {
                BSTR name = NULL;
                ITypeInfo_GetTypeAttr(info, &attr);
                ITypeInfo_GetDocumentation(info, MEMBERID_NIL, &name, NULL, NULL, NULL);
                printf(" typeinfo %ls kind %d;", name, attr->typekind);
                SysFreeString(name);
                ITypeInfo_ReleaseTypeAttr(info, attr);
                ITypeInfo_Release(info);
            }
            for (k = 0; k < ARRAY_SIZE(names); ++k)
            {
                OLECHAR *name = (OLECHAR *)names[k];
                DISPID id;
                if (SUCCEEDED(IDispatch_GetIDsOfNames(disp, &null_guid, &name, 1, LOCALE_USER_DEFAULT, &id)))
                    printf(" %ls=%ld", names[k], id);
            }
            printf("\n");
            IDispatch_Release(disp);
        }
    }
    ITrigger_Release(trigger);
    ITriggerCollection_Release(triggers);
    IAction_Release(com);
    IAction_Release(action);
    IActionCollection_Release(actions);
    IPrincipal_Release(principal);
    ITaskSettings_Release(settings);
    ITaskDefinition_Release(definition);
}

/* What reading asks of the actions other than Exec. */
static void action_reads(ITaskService *service)
{
    static const WCHAR *actions[] =
    {
        L"<ComHandler id=\"c\"><ClassId>{01234567-89ab-cdef-0123-456789abcdef}</ClassId><Data><![CDATA[<x>&]]></Data></ComHandler>",
        L"<ComHandler><Data>x</Data></ComHandler>",
        L"<ComHandler><ClassId>notaguid</ClassId></ComHandler>",
        L"<ComHandler><ClassId>01234567-89ab-cdef-0123-456789abcdef</ClassId></ComHandler>",
        L"<ShowMessage><Title>t</Title></ShowMessage>",
        L"<ShowMessage><Body>b</Body></ShowMessage>",
        L"<ShowMessage><Title>t</Title><Body>b</Body></ShowMessage>",
        L"<SendEmail><From>f</From></SendEmail>",
        L"<SendEmail><Server>s</Server></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><Attachments /></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><HeaderFields><HeaderField><Name>n</Name></HeaderField></HeaderFields></SendEmail>",
        L"<Exec />",
        L"<Exec><Command>a</Command><HideAppWindow>false</HideAppWindow></Exec>",
        L"<ComHandler />",
        L"<SendEmail><Server>s</Server><From>f</From><To>t</To></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><Body>b</Body></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><Subject>j</Subject></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><To>t</To><Body>b</Body></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><To>t</To><Subject>j</Subject></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><Subject>j</Subject><Body>b</Body></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><To>t</To><Subject>j</Subject><Body>b</Body></SendEmail>",
        L"<SendEmail><Server>s</Server><To>t</To><Subject>j</Subject><Body>b</Body></SendEmail>",
        L"<SendEmail><From>f</From><To>t</To><Subject>j</Subject><Body>b</Body></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><Cc>c</Cc><Subject>j</Subject><Body>b</Body></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><Bcc>c</Bcc><Subject>j</Subject><Body>b</Body></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><To>t</To><Subject>j</Subject><Body>b</Body><Attachments /></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><To>t</To><Subject>j</Subject><Body>b</Body><HeaderFields><HeaderField><Name>n</Name></HeaderField></HeaderFields></SendEmail>",
        L"<SendEmail><Server>s</Server><From>f</From><To>t</To><Subject>j</Subject><Body>b</Body><HeaderFields /></SendEmail>",
    };
    ITaskDefinition *definition;
    IActionCollection *collection;
    IAction *action;
    WCHAR xml[1024];
    unsigned int i;
    LONG count;
    HRESULT hr;

    printf("== action reads\n");
    for (i = 0; i < ARRAY_SIZE(actions); ++i)
    {
        if (!(definition = new_task(service))) return;
        swprintf(xml, ARRAY_SIZE(xml), L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                 "<Actions>%s</Actions></Task>", actions[i]);
        hr = ITaskDefinition_put_XmlText(definition, bstr(xml));
        printf(" %ls: hr %#lx\n", actions[i], hr);
        if (SUCCEEDED(hr))
        {
            ITaskDefinition_get_Actions(definition, &collection);
            count = 0;
            IActionCollection_get_Count(collection, &count);
            if (count && SUCCEEDED(IActionCollection_get_Item(collection, 1, &action)))
            {
                dump_action(action);
                IAction_Release(action);
            }
            IActionCollection_Release(collection);
            print_task_xml(definition);
        }
        ITaskDefinition_Release(definition);
    }
    /* the version a document declares bounds what it may hold */
    if ((definition = new_task(service)))
    {
        hr = ITaskDefinition_put_XmlText(definition, bstr(L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                "<Settings><DisallowStartOnRemoteAppSession>true</DisallowStartOnRemoteAppSession></Settings>"
                "<Actions><Exec><Command>a</Command></Exec></Actions></Task>"));
        printf(" 1.2 with DisallowStartOnRemoteAppSession: hr %#lx\n", hr);
        hr = ITaskDefinition_put_XmlText(definition, bstr(L"<Task version=\"1.3\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                "<Actions><Exec><Command>a</Command><HideAppWindow>true</HideAppWindow></Exec></Actions></Task>"));
        printf(" 1.3 with HideAppWindow: hr %#lx\n", hr);
        hr = ITaskDefinition_put_XmlText(definition, bstr(L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                "<Principals><Principal><ProcessTokenSidType>None</ProcessTokenSidType></Principal></Principals>"
                "<Actions><Exec><Command>a</Command></Exec></Actions></Task>"));
        printf(" 1.2 with ProcessTokenSidType: hr %#lx\n", hr);
        hr = ITaskDefinition_put_XmlText(definition, bstr(L"<Task version=\"1.5\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                "<Actions><Exec><Command>a</Command><HideAppWindow>true</HideAppWindow></Exec></Actions></Task>"));
        printf(" 1.5 with HideAppWindow: hr %#lx\n", hr);
        hr = ITaskDefinition_put_XmlText(definition, bstr(L"<Task version=\"1.6\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                "<Actions><Exec><Command>a</Command><HideAppWindow>true</HideAppWindow></Exec></Actions></Task>"));
        printf(" 1.6 with HideAppWindow: hr %#lx\n", hr);
        hr = ITaskDefinition_put_XmlText(definition, bstr(L"<Task version=\"1.7\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                "<Actions><Exec><Command>a</Command></Exec></Actions></Task>"));
        printf(" 1.7: hr %#lx\n", hr);
        hr = ITaskDefinition_put_XmlText(definition, bstr(L"<Task version=\"1.4\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                "<Actions><Exec><Command>a</Command></Exec></Actions></Task>"));
        printf(" 1.4 alone: hr %#lx\n", hr);
        print_task_xml(definition);
        ITaskDefinition_Release(definition);
    }
}

int main(void)
{
    ITaskService *service;
    VARIANT empty;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
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
    security_descriptor(service);
    part_xml(service);
    action_types(service);
    enumerators(service);
    additions(service);
    versions(service);
    dispatch(service);
    dispatch_types(service);
    action_reads(service);
    ITaskService_Release(service);
    CoUninitialize();
    printf("done\n");
    return 0;
}
