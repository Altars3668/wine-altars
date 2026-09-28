/*
 * What the Task Scheduler makes of a task's XML: each of the Office tasks as Windows exports them goes into a new
 * ITaskDefinition through put_XmlText, and everything the object model then says about it is printed -- the
 * registration info, the principal, the settings with their idle and network settings, each trigger with what its
 * type adds, each action -- and then the XML it writes back.  After that, a task built through the object model
 * with a trigger of every kind, and what put_XmlText says about XML it cannot take.  Nothing is registered.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <taskschd.h>
#include <stdio.h>
#include "tasks.h"

static void print_bstr(const char *name, HRESULT hr, BSTR value)
{
    if (FAILED(hr))
        printf("  %s: hr %#lx\n", name, hr);
    else
        printf("  %s: %ls\n", name, value ? value : L"(null)");
    SysFreeString(value);
}

#define STR(type, obj, prop) \
    do { BSTR v_ = NULL; HRESULT hr_ = type##_get_##prop(obj, &v_); print_bstr(#prop, hr_, v_); } while (0)
#define BOOLEAN(type, obj, prop) \
    do { VARIANT_BOOL v_ = 7; HRESULT hr_ = type##_get_##prop(obj, &v_); \
         if (FAILED(hr_)) printf("  %s: hr %#lx\n", #prop, hr_); else printf("  %s: %d\n", #prop, v_); } while (0)
#define NUMBER(type, obj, prop, t) \
    do { t v_ = (t)0x7777; HRESULT hr_ = type##_get_##prop(obj, &v_); \
         if (FAILED(hr_)) printf("  %s: hr %#lx\n", #prop, hr_); else printf("  %s: %ld\n", #prop, (long)v_); } while (0)

static void dump_trigger(ITrigger *trigger)
{
    IRepetitionPattern *repetition;
    TASK_TRIGGER_TYPE2 type = -1;
    void *specific;
    HRESULT hr;

    hr = ITrigger_get_Type(trigger, &type);
    printf(" trigger type %d (hr %#lx)\n", type, hr);
    STR(ITrigger, trigger, Id);
    STR(ITrigger, trigger, StartBoundary);
    STR(ITrigger, trigger, EndBoundary);
    STR(ITrigger, trigger, ExecutionTimeLimit);
    BOOLEAN(ITrigger, trigger, Enabled);
    if (SUCCEEDED(hr = ITrigger_get_Repetition(trigger, &repetition)))
    {
        STR(IRepetitionPattern, repetition, Interval);
        STR(IRepetitionPattern, repetition, Duration);
        BOOLEAN(IRepetitionPattern, repetition, StopAtDurationEnd);
        IRepetitionPattern_Release(repetition);
    }
    else
    {
        printf("  Repetition: hr %#lx\n", hr);
    }

    switch (type)
    {
        case TASK_TRIGGER_DAILY:
            if (FAILED(ITrigger_QueryInterface(trigger, &IID_IDailyTrigger, &specific))) break;
            NUMBER(IDailyTrigger, specific, DaysInterval, short);
            STR(IDailyTrigger, specific, RandomDelay);
            IDailyTrigger_Release((IDailyTrigger *)specific);
            break;
        case TASK_TRIGGER_WEEKLY:
            if (FAILED(ITrigger_QueryInterface(trigger, &IID_IWeeklyTrigger, &specific))) break;
            NUMBER(IWeeklyTrigger, specific, DaysOfWeek, short);
            NUMBER(IWeeklyTrigger, specific, WeeksInterval, short);
            STR(IWeeklyTrigger, specific, RandomDelay);
            IWeeklyTrigger_Release((IWeeklyTrigger *)specific);
            break;
        case TASK_TRIGGER_MONTHLY:
            if (FAILED(ITrigger_QueryInterface(trigger, &IID_IMonthlyTrigger, &specific))) break;
            NUMBER(IMonthlyTrigger, specific, DaysOfMonth, LONG);
            NUMBER(IMonthlyTrigger, specific, MonthsOfYear, short);
            BOOLEAN(IMonthlyTrigger, specific, RunOnLastDayOfMonth);
            STR(IMonthlyTrigger, specific, RandomDelay);
            IMonthlyTrigger_Release((IMonthlyTrigger *)specific);
            break;
        case TASK_TRIGGER_MONTHLYDOW:
            if (FAILED(ITrigger_QueryInterface(trigger, &IID_IMonthlyDOWTrigger, &specific))) break;
            NUMBER(IMonthlyDOWTrigger, specific, DaysOfWeek, short);
            NUMBER(IMonthlyDOWTrigger, specific, WeeksOfMonth, short);
            NUMBER(IMonthlyDOWTrigger, specific, MonthsOfYear, short);
            BOOLEAN(IMonthlyDOWTrigger, specific, RunOnLastWeekOfMonth);
            STR(IMonthlyDOWTrigger, specific, RandomDelay);
            IMonthlyDOWTrigger_Release((IMonthlyDOWTrigger *)specific);
            break;
        case TASK_TRIGGER_LOGON:
            if (FAILED(ITrigger_QueryInterface(trigger, &IID_ILogonTrigger, &specific))) break;
            STR(ILogonTrigger, specific, Delay);
            STR(ILogonTrigger, specific, UserId);
            ILogonTrigger_Release((ILogonTrigger *)specific);
            break;
        case TASK_TRIGGER_TIME:
            if (FAILED(ITrigger_QueryInterface(trigger, &IID_ITimeTrigger, &specific))) break;
            STR(ITimeTrigger, specific, RandomDelay);
            ITimeTrigger_Release((ITimeTrigger *)specific);
            break;
        case TASK_TRIGGER_BOOT:
            if (FAILED(ITrigger_QueryInterface(trigger, &IID_IBootTrigger, &specific))) break;
            STR(IBootTrigger, specific, Delay);
            IBootTrigger_Release((IBootTrigger *)specific);
            break;
        case TASK_TRIGGER_REGISTRATION:
            if (FAILED(ITrigger_QueryInterface(trigger, &IID_IRegistrationTrigger, &specific))) break;
            STR(IRegistrationTrigger, specific, Delay);
            IRegistrationTrigger_Release((IRegistrationTrigger *)specific);
            break;
        case TASK_TRIGGER_EVENT:
            if (FAILED(ITrigger_QueryInterface(trigger, &IID_IEventTrigger, &specific))) break;
            STR(IEventTrigger, specific, Subscription);
            STR(IEventTrigger, specific, Delay);
            IEventTrigger_Release((IEventTrigger *)specific);
            break;
        case TASK_TRIGGER_SESSION_STATE_CHANGE:
            if (FAILED(ITrigger_QueryInterface(trigger, &IID_ISessionStateChangeTrigger, &specific))) break;
            STR(ISessionStateChangeTrigger, specific, Delay);
            STR(ISessionStateChangeTrigger, specific, UserId);
            NUMBER(ISessionStateChangeTrigger, specific, StateChange, TASK_SESSION_STATE_CHANGE_TYPE);
            ISessionStateChangeTrigger_Release((ISessionStateChangeTrigger *)specific);
            break;
        default:
            break;
    }
}

static void dump_definition(ITaskDefinition *definition)
{
    ITriggerCollection *triggers;
    IActionCollection *actions;
    INetworkSettings *network;
    ITaskSettings2 *settings2;
    ITaskSettings *settings;
    IRegistrationInfo *info;
    IPrincipal *principal;
    IIdleSettings *idle;
    ITrigger *trigger;
    IAction *action;
    LONG count, i;
    BSTR xml;
    HRESULT hr;

    if (SUCCEEDED(hr = ITaskDefinition_get_RegistrationInfo(definition, &info)))
    {
        printf(" registration info\n");
        STR(IRegistrationInfo, info, Description);
        STR(IRegistrationInfo, info, Author);
        STR(IRegistrationInfo, info, Version);
        STR(IRegistrationInfo, info, Date);
        STR(IRegistrationInfo, info, Documentation);
        STR(IRegistrationInfo, info, URI);
        STR(IRegistrationInfo, info, Source);
        IRegistrationInfo_Release(info);
    }
    else printf(" registration info: hr %#lx\n", hr);

    if (SUCCEEDED(hr = ITaskDefinition_get_Principal(definition, &principal)))
    {
        printf(" principal\n");
        STR(IPrincipal, principal, Id);
        STR(IPrincipal, principal, DisplayName);
        STR(IPrincipal, principal, UserId);
        NUMBER(IPrincipal, principal, LogonType, TASK_LOGON_TYPE);
        STR(IPrincipal, principal, GroupId);
        NUMBER(IPrincipal, principal, RunLevel, TASK_RUNLEVEL_TYPE);
        IPrincipal_Release(principal);
    }
    else printf(" principal: hr %#lx\n", hr);

    if (SUCCEEDED(hr = ITaskDefinition_get_Settings(definition, &settings)))
    {
        printf(" settings\n");
        BOOLEAN(ITaskSettings, settings, AllowDemandStart);
        STR(ITaskSettings, settings, RestartInterval);
        NUMBER(ITaskSettings, settings, RestartCount, INT);
        NUMBER(ITaskSettings, settings, MultipleInstances, TASK_INSTANCES_POLICY);
        BOOLEAN(ITaskSettings, settings, StopIfGoingOnBatteries);
        BOOLEAN(ITaskSettings, settings, DisallowStartIfOnBatteries);
        BOOLEAN(ITaskSettings, settings, AllowHardTerminate);
        BOOLEAN(ITaskSettings, settings, StartWhenAvailable);
        BOOLEAN(ITaskSettings, settings, RunOnlyIfNetworkAvailable);
        STR(ITaskSettings, settings, ExecutionTimeLimit);
        BOOLEAN(ITaskSettings, settings, Enabled);
        STR(ITaskSettings, settings, DeleteExpiredTaskAfter);
        NUMBER(ITaskSettings, settings, Priority, INT);
        NUMBER(ITaskSettings, settings, Compatibility, TASK_COMPATIBILITY);
        BOOLEAN(ITaskSettings, settings, Hidden);
        BOOLEAN(ITaskSettings, settings, RunOnlyIfIdle);
        BOOLEAN(ITaskSettings, settings, WakeToRun);
        if (SUCCEEDED(hr = ITaskSettings_get_IdleSettings(settings, &idle)))
        {
            STR(IIdleSettings, idle, IdleDuration);
            STR(IIdleSettings, idle, WaitTimeout);
            BOOLEAN(IIdleSettings, idle, StopOnIdleEnd);
            BOOLEAN(IIdleSettings, idle, RestartOnIdle);
            IIdleSettings_Release(idle);
        }
        else printf("  IdleSettings: hr %#lx\n", hr);
        if (SUCCEEDED(hr = ITaskSettings_get_NetworkSettings(settings, &network)))
        {
            STR(INetworkSettings, network, Name);
            STR(INetworkSettings, network, Id);
            INetworkSettings_Release(network);
        }
        else printf("  NetworkSettings: hr %#lx\n", hr);
        if (SUCCEEDED(hr = ITaskSettings_QueryInterface(settings, &IID_ITaskSettings2, (void **)&settings2)))
        {
            BOOLEAN(ITaskSettings2, settings2, DisallowStartOnRemoteAppSession);
            BOOLEAN(ITaskSettings2, settings2, UseUnifiedSchedulingEngine);
            ITaskSettings2_Release(settings2);
        }
        else printf("  ITaskSettings2: hr %#lx\n", hr);
        ITaskSettings_Release(settings);
    }
    else printf(" settings: hr %#lx\n", hr);

    if (SUCCEEDED(hr = ITaskDefinition_get_Triggers(definition, &triggers)))
    {
        count = -1;
        hr = ITriggerCollection_get_Count(triggers, &count);
        printf(" triggers: %ld (hr %#lx)\n", count, hr);
        for (i = 1; i <= count; ++i)
        {
            if (FAILED(hr = ITriggerCollection_get_Item(triggers, i, &trigger)))
            {
                printf(" trigger %ld: hr %#lx\n", i, hr);
                continue;
            }
            dump_trigger(trigger);
            ITrigger_Release(trigger);
        }
        ITriggerCollection_Release(triggers);
    }
    else printf(" triggers: hr %#lx\n", hr);

    if (SUCCEEDED(hr = ITaskDefinition_get_Actions(definition, &actions)))
    {
        count = -1;
        hr = IActionCollection_get_Count(actions, &count);
        printf(" actions: %ld (hr %#lx)\n", count, hr);
        STR(IActionCollection, actions, Context);
        for (i = 1; i <= count; ++i)
        {
            TASK_ACTION_TYPE type = -1;
            IExecAction *exec;

            if (FAILED(hr = IActionCollection_get_Item(actions, i, &action)))
            {
                printf(" action %ld: hr %#lx\n", i, hr);
                continue;
            }
            hr = IAction_get_Type(action, &type);
            printf(" action type %d (hr %#lx)\n", type, hr);
            STR(IAction, action, Id);
            if (type == TASK_ACTION_EXEC && SUCCEEDED(IAction_QueryInterface(action, &IID_IExecAction, (void **)&exec)))
            {
                STR(IExecAction, exec, Path);
                STR(IExecAction, exec, Arguments);
                STR(IExecAction, exec, WorkingDirectory);
                IExecAction_Release(exec);
            }
            IAction_Release(action);
        }
        IActionCollection_Release(actions);
    }
    else printf(" actions: hr %#lx\n", hr);

    xml = NULL;
    hr = ITaskDefinition_get_XmlText(definition, &xml);
    printf(" xml (hr %#lx):\n%ls\n", hr, xml ? xml : L"(null)");
    SysFreeString(xml);
}

static void load(ITaskService *service, const char *name, const WCHAR *text)
{
    ITaskDefinition *definition;
    BSTR xml;
    HRESULT hr;

    if (FAILED(hr = ITaskService_NewTask(service, 0, &definition)))
    {
        printf("NewTask %#lx\n", hr);
        return;
    }
    xml = SysAllocString(text);
    hr = ITaskDefinition_put_XmlText(definition, xml);
    SysFreeString(xml);
    printf("== %s: put_XmlText %#lx\n", name, hr);
    if (SUCCEEDED(hr))
        dump_definition(definition);
    ITaskDefinition_Release(definition);
}

static BSTR bstr(const WCHAR *s)
{
    static BSTR strings[64];
    static unsigned int next;

    SysFreeString(strings[next % 64]);
    return strings[next++ % 64] = SysAllocString(s);
}

/* A task with a trigger of each kind, built through the object model. */
static void build(ITaskService *service)
{
    static const TASK_TRIGGER_TYPE2 types[] =
    {
        TASK_TRIGGER_EVENT, TASK_TRIGGER_TIME, TASK_TRIGGER_DAILY, TASK_TRIGGER_WEEKLY, TASK_TRIGGER_MONTHLY,
        TASK_TRIGGER_MONTHLYDOW, TASK_TRIGGER_IDLE, TASK_TRIGGER_REGISTRATION, TASK_TRIGGER_BOOT,
        TASK_TRIGGER_LOGON, TASK_TRIGGER_SESSION_STATE_CHANGE,
    };
    ITaskDefinition *definition;
    ITriggerCollection *triggers;
    IActionCollection *actions;
    IRegistrationInfo *info;
    ITaskSettings *settings;
    IPrincipal *principal;
    IExecAction *exec;
    ITrigger *trigger;
    IAction *action;
    unsigned int i;
    void *specific;
    HRESULT hr;

    if (FAILED(hr = ITaskService_NewTask(service, 0, &definition)))
    {
        printf("NewTask %#lx\n", hr);
        return;
    }

    printf("== new task, untouched\n");
    dump_definition(definition);

    ITaskDefinition_get_RegistrationInfo(definition, &info);
    IRegistrationInfo_put_Author(info, bstr(L"Wine Altars"));
    IRegistrationInfo_put_Description(info, bstr(L"built through the object model"));
    IRegistrationInfo_Release(info);

    ITaskDefinition_get_Principal(definition, &principal);
    hr = IPrincipal_put_GroupId(principal, bstr(L"S-1-5-32-545"));
    printf("put_GroupId %#lx\n", hr);
    hr = IPrincipal_put_RunLevel(principal, TASK_RUNLEVEL_HIGHEST);
    printf("put_RunLevel %#lx\n", hr);
    IPrincipal_Release(principal);

    ITaskDefinition_get_Settings(definition, &settings);
    ITaskSettings_put_StartWhenAvailable(settings, VARIANT_TRUE);
    ITaskSettings_put_RestartCount(settings, 3);
    ITaskSettings_put_RestartInterval(settings, bstr(L"PT30M"));
    ITaskSettings_Release(settings);

    ITaskDefinition_get_Triggers(definition, &triggers);
    for (i = 0; i < ARRAY_SIZE(types); ++i)
    {
        if (FAILED(hr = ITriggerCollection_Create(triggers, types[i], &trigger)))
        {
            printf("Create trigger %d: hr %#lx\n", types[i], hr);
            continue;
        }
        ITrigger_put_StartBoundary(trigger, bstr(L"2020-01-02T03:04:05"));
        switch (types[i])
        {
            case TASK_TRIGGER_DAILY:
                ITrigger_QueryInterface(trigger, &IID_IDailyTrigger, &specific);
                IDailyTrigger_put_DaysInterval((IDailyTrigger *)specific, 2);
                IDailyTrigger_put_RandomDelay((IDailyTrigger *)specific, bstr(L"PT4H"));
                IDailyTrigger_Release((IDailyTrigger *)specific);
                break;
            case TASK_TRIGGER_WEEKLY:
                ITrigger_QueryInterface(trigger, &IID_IWeeklyTrigger, &specific);
                IWeeklyTrigger_put_DaysOfWeek((IWeeklyTrigger *)specific, 0x2a);
                IWeeklyTrigger_put_WeeksInterval((IWeeklyTrigger *)specific, 3);
                IWeeklyTrigger_Release((IWeeklyTrigger *)specific);
                break;
            case TASK_TRIGGER_MONTHLY:
                ITrigger_QueryInterface(trigger, &IID_IMonthlyTrigger, &specific);
                IMonthlyTrigger_put_DaysOfMonth((IMonthlyTrigger *)specific, 0x10001);
                IMonthlyTrigger_put_MonthsOfYear((IMonthlyTrigger *)specific, 0x801);
                IMonthlyTrigger_Release((IMonthlyTrigger *)specific);
                break;
            case TASK_TRIGGER_MONTHLYDOW:
                ITrigger_QueryInterface(trigger, &IID_IMonthlyDOWTrigger, &specific);
                IMonthlyDOWTrigger_put_DaysOfWeek((IMonthlyDOWTrigger *)specific, 0x41);
                IMonthlyDOWTrigger_put_WeeksOfMonth((IMonthlyDOWTrigger *)specific, 0x3);
                IMonthlyDOWTrigger_put_MonthsOfYear((IMonthlyDOWTrigger *)specific, 0xfff);
                IMonthlyDOWTrigger_Release((IMonthlyDOWTrigger *)specific);
                break;
            case TASK_TRIGGER_LOGON:
                ITrigger_QueryInterface(trigger, &IID_ILogonTrigger, &specific);
                ILogonTrigger_put_Delay((ILogonTrigger *)specific, bstr(L"PT5M"));
                ILogonTrigger_Release((ILogonTrigger *)specific);
                break;
            case TASK_TRIGGER_EVENT:
                ITrigger_QueryInterface(trigger, &IID_IEventTrigger, &specific);
                IEventTrigger_put_Subscription((IEventTrigger *)specific,
                        bstr(L"<QueryList><Query Id=\"0\" Path=\"System\"><Select Path=\"System\">*</Select></Query></QueryList>"));
                IEventTrigger_Release((IEventTrigger *)specific);
                break;
            case TASK_TRIGGER_SESSION_STATE_CHANGE:
                ITrigger_QueryInterface(trigger, &IID_ISessionStateChangeTrigger, &specific);
                ISessionStateChangeTrigger_put_StateChange((ISessionStateChangeTrigger *)specific,
                        TASK_SESSION_UNLOCK);
                ISessionStateChangeTrigger_Release((ISessionStateChangeTrigger *)specific);
                break;
            default:
                break;
        }
        ITrigger_Release(trigger);
    }
    ITriggerCollection_Release(triggers);

    ITaskDefinition_get_Actions(definition, &actions);
    hr = IActionCollection_Create(actions, TASK_ACTION_EXEC, &action);
    printf("Create action %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        IAction_QueryInterface(action, &IID_IExecAction, (void **)&exec);
        IExecAction_put_Path(exec, bstr(L"C:\\Windows\\notepad.exe"));
        IExecAction_put_Arguments(exec, bstr(L"/a b"));
        IExecAction_Release(exec);
        IAction_Release(action);
    }
    IActionCollection_Release(actions);

    printf("== built\n");
    dump_definition(definition);
    ITaskDefinition_Release(definition);
}

int main(void)
{
    static const struct
    {
        const char *name;
        const WCHAR *xml;
    }
    bad[] =
    {
        {"unknown element", L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Foo/></Task>"},
        {"unclosed", L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Settings>"},
        {"bad boundary", L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Triggers>"
                "<TimeTrigger><StartBoundary>tomorrow</StartBoundary></TimeTrigger></Triggers>"
                "<Actions><Exec><Command>a</Command></Exec></Actions></Task>"},
        {"no actions", L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"></Task>"},
        {"unknown trigger", L"<Task xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Triggers>"
                "<FooTrigger/></Triggers><Actions><Exec><Command>a</Command></Exec></Actions></Task>"},
    };
    VARIANT empty;
    ITaskService *service;
    unsigned int i;
    HRESULT hr;

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

    for (i = 0; i < ARRAY_SIZE(office_tasks); ++i)
        load(service, office_tasks[i].name, office_tasks[i].xml);
    build(service);
    for (i = 0; i < ARRAY_SIZE(bad); ++i)
        load(service, bad[i].name, bad[i].xml);

    ITaskService_Release(service);
    CoUninitialize();
    printf("done\n");
    return 0;
}
