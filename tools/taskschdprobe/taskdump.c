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
    if (xml)
    {
        unsigned int crs = 0, lfs = 0;
        const WCHAR *p;

        for (p = xml; *p; ++p)
        {
            crs += *p == '\r';
            lfs += *p == '\n';
        }
        printf(" xml: %u characters, %u CR, %u LF, last %#x\n", (unsigned int)(p - xml), crs, lfs,
                p > xml ? p[-1] : 0);
    }
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

/* A task with everything set that can be, built through the object model, then read back from its own XML. */
static void build_everything(ITaskService *service)
{
    static const TASK_TRIGGER_TYPE2 types[] =
    {
        TASK_TRIGGER_EVENT, TASK_TRIGGER_TIME, TASK_TRIGGER_DAILY, TASK_TRIGGER_WEEKLY, TASK_TRIGGER_MONTHLY,
        TASK_TRIGGER_MONTHLYDOW, TASK_TRIGGER_IDLE, TASK_TRIGGER_REGISTRATION, TASK_TRIGGER_BOOT,
        TASK_TRIGGER_LOGON, TASK_TRIGGER_SESSION_STATE_CHANGE,
    };
    ITaskDefinition *definition, *reread;
    ITaskNamedValueCollection *values;
    ITaskNamedValuePair *pair;
    IRepetitionPattern *repetition;
    ITriggerCollection *triggers;
    IActionCollection *actions;
    INetworkSettings *network;
    ITaskSettings2 *settings2;
    IRegistrationInfo *info;
    ITaskSettings *settings;
    IPrincipal *principal;
    IIdleSettings *idle;
    IExecAction *exec;
    ITrigger *trigger;
    IAction *action;
    WCHAR id[16];
    unsigned int i;
    void *specific;
    BSTR xml;
    HRESULT hr;

    if (FAILED(hr = ITaskService_NewTask(service, 0, &definition)))
    {
        printf("NewTask %#lx\n", hr);
        return;
    }
    ITaskDefinition_put_Data(definition, bstr(L"some <data> & more"));

    ITaskDefinition_get_RegistrationInfo(definition, &info);
    IRegistrationInfo_put_Author(info, bstr(L"Wine Altars"));
    IRegistrationInfo_put_Description(info, bstr(L"everything"));
    IRegistrationInfo_put_Version(info, bstr(L"1.2.3"));
    IRegistrationInfo_put_Date(info, bstr(L"2020-01-02T03:04:05"));
    IRegistrationInfo_put_Documentation(info, bstr(L"docs"));
    IRegistrationInfo_put_URI(info, bstr(L"\\Wine\\Everything"));
    IRegistrationInfo_put_Source(info, bstr(L"source"));
    IRegistrationInfo_Release(info);

    ITaskDefinition_get_Principal(definition, &principal);
    IPrincipal_put_Id(principal, bstr(L"Author"));
    IPrincipal_put_DisplayName(principal, bstr(L"display"));
    IPrincipal_put_UserId(principal, bstr(L"S-1-5-18"));
    hr = IPrincipal_put_LogonType(principal, TASK_LOGON_SERVICE_ACCOUNT);
    printf("put_LogonType %#lx\n", hr);
    IPrincipal_put_RunLevel(principal, TASK_RUNLEVEL_HIGHEST);
    IPrincipal_Release(principal);

    ITaskDefinition_get_Settings(definition, &settings);
    ITaskSettings_put_AllowDemandStart(settings, VARIANT_FALSE);
    ITaskSettings_put_RestartInterval(settings, bstr(L"PT2M"));
    ITaskSettings_put_RestartCount(settings, 4);
    ITaskSettings_put_MultipleInstances(settings, TASK_INSTANCES_PARALLEL);
    ITaskSettings_put_StopIfGoingOnBatteries(settings, VARIANT_FALSE);
    ITaskSettings_put_DisallowStartIfOnBatteries(settings, VARIANT_FALSE);
    ITaskSettings_put_AllowHardTerminate(settings, VARIANT_FALSE);
    ITaskSettings_put_StartWhenAvailable(settings, VARIANT_TRUE);
    ITaskSettings_put_RunOnlyIfNetworkAvailable(settings, VARIANT_TRUE);
    ITaskSettings_put_ExecutionTimeLimit(settings, bstr(L"PT3H"));
    ITaskSettings_put_Enabled(settings, VARIANT_FALSE);
    ITaskSettings_put_DeleteExpiredTaskAfter(settings, bstr(L"P30D"));
    ITaskSettings_put_Priority(settings, 5);
    hr = ITaskSettings_put_Compatibility(settings, TASK_COMPATIBILITY_V2_1);
    printf("put_Compatibility %#lx\n", hr);
    ITaskSettings_put_Hidden(settings, VARIANT_TRUE);
    ITaskSettings_put_RunOnlyIfIdle(settings, VARIANT_TRUE);
    ITaskSettings_put_WakeToRun(settings, VARIANT_TRUE);
    if (SUCCEEDED(ITaskSettings_get_IdleSettings(settings, &idle)))
    {
        IIdleSettings_put_IdleDuration(idle, bstr(L"PT7M"));
        IIdleSettings_put_WaitTimeout(idle, bstr(L"PT2H"));
        IIdleSettings_put_StopOnIdleEnd(idle, VARIANT_FALSE);
        IIdleSettings_put_RestartOnIdle(idle, VARIANT_TRUE);
        IIdleSettings_Release(idle);
    }
    if (SUCCEEDED(ITaskSettings_get_NetworkSettings(settings, &network)))
    {
        INetworkSettings_put_Name(network, bstr(L"network"));
        INetworkSettings_put_Id(network, bstr(L"{01234567-89ab-cdef-0123-456789abcdef}"));
        INetworkSettings_Release(network);
    }
    if (SUCCEEDED(ITaskSettings_QueryInterface(settings, &IID_ITaskSettings2, (void **)&settings2)))
    {
        ITaskSettings2_put_DisallowStartOnRemoteAppSession(settings2, VARIANT_TRUE);
        ITaskSettings2_put_UseUnifiedSchedulingEngine(settings2, VARIANT_TRUE);
        ITaskSettings2_Release(settings2);
    }
    ITaskSettings_Release(settings);

    ITaskDefinition_get_Triggers(definition, &triggers);
    for (i = 0; i < ARRAY_SIZE(types); ++i)
    {
        if (FAILED(hr = ITriggerCollection_Create(triggers, types[i], &trigger)))
        {
            printf("Create trigger %d: hr %#lx\n", types[i], hr);
            continue;
        }
        swprintf(id, ARRAY_SIZE(id), L"t%u", i);
        ITrigger_put_Id(trigger, bstr(id));
        ITrigger_put_StartBoundary(trigger, bstr(L"2020-01-02T03:04:05"));
        ITrigger_put_EndBoundary(trigger, bstr(L"2030-01-02T03:04:05"));
        ITrigger_put_ExecutionTimeLimit(trigger, bstr(L"PT9M"));
        ITrigger_put_Enabled(trigger, VARIANT_FALSE);
        if (SUCCEEDED(ITrigger_get_Repetition(trigger, &repetition)))
        {
            IRepetitionPattern_put_Interval(repetition, bstr(L"PT5M"));
            IRepetitionPattern_put_Duration(repetition, bstr(L"PT1H"));
            IRepetitionPattern_put_StopAtDurationEnd(repetition, VARIANT_TRUE);
            IRepetitionPattern_Release(repetition);
        }
        switch (types[i])
        {
            case TASK_TRIGGER_EVENT:
                ITrigger_QueryInterface(trigger, &IID_IEventTrigger, &specific);
                IEventTrigger_put_Subscription((IEventTrigger *)specific, bstr(L"<QueryList/>"));
                IEventTrigger_put_Delay((IEventTrigger *)specific, bstr(L"PT1M"));
                if (SUCCEEDED(IEventTrigger_get_ValueQueries((IEventTrigger *)specific, &values)))
                {
                    if (SUCCEEDED(ITaskNamedValueCollection_Create(values, bstr(L"name"), bstr(L"value"), &pair)))
                        ITaskNamedValuePair_Release(pair);
                    ITaskNamedValueCollection_Release(values);
                }
                IEventTrigger_Release((IEventTrigger *)specific);
                break;
            case TASK_TRIGGER_TIME:
                ITrigger_QueryInterface(trigger, &IID_ITimeTrigger, &specific);
                ITimeTrigger_put_RandomDelay((ITimeTrigger *)specific, bstr(L"PT2M"));
                ITimeTrigger_Release((ITimeTrigger *)specific);
                break;
            case TASK_TRIGGER_DAILY:
                ITrigger_QueryInterface(trigger, &IID_IDailyTrigger, &specific);
                IDailyTrigger_put_DaysInterval((IDailyTrigger *)specific, 3);
                IDailyTrigger_put_RandomDelay((IDailyTrigger *)specific, bstr(L"PT3M"));
                IDailyTrigger_Release((IDailyTrigger *)specific);
                break;
            case TASK_TRIGGER_WEEKLY:
                ITrigger_QueryInterface(trigger, &IID_IWeeklyTrigger, &specific);
                IWeeklyTrigger_put_DaysOfWeek((IWeeklyTrigger *)specific, 0x41);
                IWeeklyTrigger_put_WeeksInterval((IWeeklyTrigger *)specific, 2);
                IWeeklyTrigger_put_RandomDelay((IWeeklyTrigger *)specific, bstr(L"PT4M"));
                IWeeklyTrigger_Release((IWeeklyTrigger *)specific);
                break;
            case TASK_TRIGGER_MONTHLY:
                ITrigger_QueryInterface(trigger, &IID_IMonthlyTrigger, &specific);
                IMonthlyTrigger_put_DaysOfMonth((IMonthlyTrigger *)specific, 0x40000002);
                IMonthlyTrigger_put_MonthsOfYear((IMonthlyTrigger *)specific, 0x6);
                IMonthlyTrigger_put_RunOnLastDayOfMonth((IMonthlyTrigger *)specific, VARIANT_TRUE);
                IMonthlyTrigger_put_RandomDelay((IMonthlyTrigger *)specific, bstr(L"PT5M"));
                IMonthlyTrigger_Release((IMonthlyTrigger *)specific);
                break;
            case TASK_TRIGGER_MONTHLYDOW:
                ITrigger_QueryInterface(trigger, &IID_IMonthlyDOWTrigger, &specific);
                IMonthlyDOWTrigger_put_DaysOfWeek((IMonthlyDOWTrigger *)specific, 0x2);
                IMonthlyDOWTrigger_put_WeeksOfMonth((IMonthlyDOWTrigger *)specific, 0x8);
                IMonthlyDOWTrigger_put_MonthsOfYear((IMonthlyDOWTrigger *)specific, 0x1);
                IMonthlyDOWTrigger_put_RunOnLastWeekOfMonth((IMonthlyDOWTrigger *)specific, VARIANT_TRUE);
                IMonthlyDOWTrigger_put_RandomDelay((IMonthlyDOWTrigger *)specific, bstr(L"PT6M"));
                IMonthlyDOWTrigger_Release((IMonthlyDOWTrigger *)specific);
                break;
            case TASK_TRIGGER_LOGON:
                ITrigger_QueryInterface(trigger, &IID_ILogonTrigger, &specific);
                ILogonTrigger_put_Delay((ILogonTrigger *)specific, bstr(L"PT7M"));
                ILogonTrigger_put_UserId((ILogonTrigger *)specific, bstr(L"S-1-5-18"));
                ILogonTrigger_Release((ILogonTrigger *)specific);
                break;
            case TASK_TRIGGER_BOOT:
                ITrigger_QueryInterface(trigger, &IID_IBootTrigger, &specific);
                IBootTrigger_put_Delay((IBootTrigger *)specific, bstr(L"PT8M"));
                IBootTrigger_Release((IBootTrigger *)specific);
                break;
            case TASK_TRIGGER_REGISTRATION:
                ITrigger_QueryInterface(trigger, &IID_IRegistrationTrigger, &specific);
                IRegistrationTrigger_put_Delay((IRegistrationTrigger *)specific, bstr(L"PT9M"));
                IRegistrationTrigger_Release((IRegistrationTrigger *)specific);
                break;
            case TASK_TRIGGER_SESSION_STATE_CHANGE:
                ITrigger_QueryInterface(trigger, &IID_ISessionStateChangeTrigger, &specific);
                ISessionStateChangeTrigger_put_Delay((ISessionStateChangeTrigger *)specific, bstr(L"PT10M"));
                ISessionStateChangeTrigger_put_UserId((ISessionStateChangeTrigger *)specific, bstr(L"S-1-5-18"));
                ISessionStateChangeTrigger_put_StateChange((ISessionStateChangeTrigger *)specific,
                        TASK_CONSOLE_CONNECT);
                ISessionStateChangeTrigger_Release((ISessionStateChangeTrigger *)specific);
                break;
            default:
                break;
        }
        ITrigger_Release(trigger);
    }
    ITriggerCollection_Release(triggers);

    ITaskDefinition_get_Actions(definition, &actions);
    IActionCollection_put_Context(actions, bstr(L"Author"));
    for (i = 0; i < 2; ++i)
    {
        if (FAILED(hr = IActionCollection_Create(actions, TASK_ACTION_EXEC, &action)))
        {
            printf("Create action %#lx\n", hr);
            break;
        }
        swprintf(id, ARRAY_SIZE(id), L"a%u", i);
        IAction_put_Id(action, bstr(id));
        IAction_QueryInterface(action, &IID_IExecAction, (void **)&exec);
        IExecAction_put_Path(exec, bstr(L"C:\\Windows\\notepad.exe"));
        IExecAction_put_Arguments(exec, bstr(L"\"quoted <arg>\""));
        IExecAction_put_WorkingDirectory(exec, bstr(L"C:\\Windows"));
        IExecAction_Release(exec);
        IAction_Release(action);
    }
    IActionCollection_Release(actions);

    printf("== everything\n");
    dump_definition(definition);
    xml = NULL;
    ITaskDefinition_get_XmlText(definition, &xml);
    ITaskDefinition_Release(definition);

    if (xml && SUCCEEDED(ITaskService_NewTask(service, 0, &reread)))
    {
        hr = ITaskDefinition_put_XmlText(reread, xml);
        printf("== everything, read back: put_XmlText %#lx\n", hr);
        if (SUCCEEDED(hr))
            dump_definition(reread);
        ITaskDefinition_Release(reread);
    }
    SysFreeString(xml);
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
    build_everything(service);
    for (i = 0; i < ARRAY_SIZE(bad); ++i)
        load(service, bad[i].name, bad[i].xml);

    ITaskService_Release(service);
    CoUninitialize();
    printf("done\n");
    return 0;
}
