/* What the C++ runtime's time zone database answers: std::chrono::get_tzdb(), current_zone() and
 * every time_zone's get_info() end in msvcp140_atomic_wait.dll's __std_tzdb_* exports, which on
 * Windows go through icu.dll.  Word asks for the database and the current zone at every start;
 * Wine answers with Windows' own zone names, and __std_tzdb_get_sys_info, which every conversion
 * needs, is a stub that raises an exception.
 *
 * This prints: the database's version, size and a sample of its names and links; the shape of the
 * current zone's name (not the name itself); get_sys_info for fixed zones and instants, a link, a
 * name in the wrong case and a name that does not exist; the leap seconds; and what icu.dll itself
 * says about the data version and whether the host zone is known to it.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror tzdbprobe.c -o tzdbprobe.exe
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct tzdb_time_zones { int err; const char *version; size_t count; const char **names; const char **links; };
struct tzdb_current_zone { int err; const char *name; };
struct tzdb_sys_info { int err; double begin, end; int32_t offset, save; const char *abbrev; };
struct tzdb_leap_second { uint16_t year, month, day, hour, negative, reserved; };

static struct tzdb_time_zones *(__stdcall *get_time_zones)(void);
static void (__stdcall *delete_time_zones)(struct tzdb_time_zones *);
static struct tzdb_current_zone *(__stdcall *get_current_zone)(void);
static void (__stdcall *delete_current_zone)(struct tzdb_current_zone *);
static struct tzdb_sys_info *(__stdcall *get_sys_info)(const char *, size_t, double);
static void (__stdcall *delete_sys_info)(struct tzdb_sys_info *);
static struct tzdb_leap_second *(__stdcall *get_leap_seconds)(size_t, size_t *);
static void (__stdcall *delete_leap_seconds)(struct tzdb_leap_second *);

static int fmod_is_zero(double ms)
{
    return ms == (double)(long long)(ms / 1000) * 1000;
}

static const char *when(double ms, char *buf)
{
    time_t t;
    struct tm *tm;

    if (ms < -1e15 || ms > 1e15)
    {
        sprintf(buf, "%.17g", ms);
        return buf;
    }
    t = (time_t)(ms / 1000);
    tm = gmtime(&t);
    if (!tm) sprintf(buf, "%.17g", ms);
    else sprintf(buf, "%04d-%02d-%02dT%02d:%02d:%02dZ%s", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec, fmod_is_zero(ms) ? "" : "+ms");
    return buf;
}

static void sys_info(const char *zone, size_t len, double ms)
{
    struct tzdb_sys_info *i = get_sys_info(zone, len, ms);
    char a[64], b[64], c[64];

    if (!i)
    {
        printf("sys_info(%.*s, %s): NULL\n", (int)len, zone, when(ms, a));
        return;
    }
    printf("sys_info(%.*s, %s): err %d", (int)len, zone, when(ms, a), i->err);
    if (!i->err)
        printf(", begin %s, end %s, offset %d, save %d, abbrev %s", when(i->begin, b), when(i->end, c),
               i->offset, i->save, i->abbrev ? i->abbrev : "(null)");
    printf("\n");
    delete_sys_info(i);
}

int main(void)
{
    HMODULE msvcp = LoadLibraryA("msvcp140_atomic_wait.dll"), icu = LoadLibraryA("icu.dll");
    static const char *wanted[] = {"America/New_York", "US/Eastern", "Asia/Shanghai", "Asia/Calcutta", "Asia/Kolkata",
                                   "UTC", "Etc/UTC", "GMT", "Europe/London", "Australia/Lord_Howe", "EST5EDT"};
    struct tzdb_time_zones *z;
    struct tzdb_current_zone *cur;
    struct tzdb_leap_second *leap;
    size_t i, j, links = 0, size = 0;

    printf("msvcp140_atomic_wait.dll %s, icu.dll %s\n", msvcp ? "loaded" : "missing", icu ? "loaded" : "missing");
    if (!msvcp) return 1;
#define GET(f, name) f = (void *)GetProcAddress(msvcp, name); if (!f) { printf("%s missing\n", name); return 1; }
    GET(get_time_zones, "__std_tzdb_get_time_zones")
    GET(delete_time_zones, "__std_tzdb_delete_time_zones")
    GET(get_current_zone, "__std_tzdb_get_current_zone")
    GET(delete_current_zone, "__std_tzdb_delete_current_zone")
    GET(get_sys_info, "__std_tzdb_get_sys_info")
    GET(delete_sys_info, "__std_tzdb_delete_sys_info")
    GET(get_leap_seconds, "__std_tzdb_get_leap_seconds")
    GET(delete_leap_seconds, "__std_tzdb_delete_leap_seconds")

    z = get_time_zones();
    printf("time_zones: err %d, version %s, count %lu\n", z->err, z->version ? z->version : "(null)", (unsigned long)z->count);
    for (i = 0; i < z->count; i++) if (z->links[i]) links++;
    printf("    %lu of them links; first names:", (unsigned long)links);
    for (i = 0; i < z->count && i < 6; i++) printf(" %s%s%s", z->names[i], z->links[i] ? "->" : "", z->links[i] ? z->links[i] : "");
    printf("\n    last: %s\n", z->count ? z->names[z->count - 1] : "-");
    for (j = 0; j < ARRAYSIZE(wanted); j++)
    {
        for (i = 0; i < z->count; i++) if (!strcmp(z->names[i], wanted[j])) break;
        if (i == z->count) printf("    %s: absent\n", wanted[j]);
        else printf("    %s: index %lu%s%s\n", wanted[j], (unsigned long)i, z->links[i] ? ", link to " : "", z->links[i] ? z->links[i] : "");
    }
    for (i = 1; i < z->count; i++) if (strcmp(z->names[i - 1], z->names[i]) >= 0) break;
    printf("    names sorted by strcmp: %s\n", i >= z->count ? "yes" : "no");

    cur = get_current_zone();
    printf("current_zone: err %d", cur->err);
    if (cur->name)
    {
        for (i = 0; i < z->count; i++) if (!strcmp(z->names[i], cur->name)) break;
        struct tzdb_sys_info *info = get_sys_info(cur->name, strlen(cur->name), 1782907200000.0);

        printf(", name %s, %lu characters, %s, %s, its sys_info err %d\n", strchr(cur->name, '/') ? "has a '/'" : "has no '/'",
               (unsigned long)strlen(cur->name), i < z->count ? "in the list" : "not in the list",
               i < z->count && z->links[i] ? "a link" : "not a link", info ? info->err : -1);
        if (info) delete_sys_info(info);
    }
    else printf(", no name\n");
    delete_current_zone(cur);
    delete_time_zones(z);

    sys_info("America/New_York", 16, 1782907200000.0);   /* 2026-07-01T12:00:00Z */
    sys_info("America/New_York", 16, 1768478400000.0);   /* 2026-01-15T12:00:00Z */
    sys_info("America/New_York", 16, 1772953199999.0);   /* a millisecond before the 2026 change */
    sys_info("America/New_York", 16, 1772953200000.0);   /* 2026-03-08T07:00:00Z, the change */
    sys_info("America/New_York", 16, -3000000000000.0);  /* 1874, before standard time */
    sys_info("Asia/Shanghai", 13, 1782907200000.0);
    sys_info("Asia/Kolkata", 12, 1782907200000.0);
    sys_info("Australia/Lord_Howe", 19, 1768478400000.0);
    sys_info("Europe/London", 13, 1782907200000.0);
    sys_info("UTC", 3, 1782907200000.0);
    sys_info("Etc/GMT+5", 9, 1782907200000.0);
    sys_info("US/Eastern", 10, 1782907200000.0);
    sys_info("america/new_york", 16, 1782907200000.0);
    sys_info("America/New_Yorkxxx", 16, 1782907200000.0); /* the length is what counts */
    sys_info("Nowhere/Nothing", 15, 1782907200000.0);

    leap = get_leap_seconds(0, &size);
    printf("leap_seconds(0): %s, size %lu\n", leap ? "data" : "NULL", (unsigned long)size);
    for (i = 0; leap && i < size && i < 4; i++)
        printf("    %u-%02u-%02u %02u:00 negative %u reserved %u\n", leap[i].year, leap[i].month, leap[i].day, leap[i].hour,
               leap[i].negative, leap[i].reserved);
    delete_leap_seconds(leap);
    leap = get_leap_seconds(size, &size);
    printf("leap_seconds(same size): %s, size %lu\n", leap ? "data" : "NULL", (unsigned long)size);
    delete_leap_seconds(leap);

    if (icu)
    {
        const char *(__cdecl *version)(int *) = (void *)GetProcAddress(icu, "ucal_getTZDataVersion");
        int (__cdecl *def)(WCHAR *, int, int *) = (void *)GetProcAddress(icu, "ucal_getDefaultTimeZone");
        int (__cdecl *host)(WCHAR *, int, int *) = (void *)GetProcAddress(icu, "ucal_getHostTimeZone");
        WCHAR buf[128];
        int err = 0, len;

        if (version) { err = 0; printf("icu tz data version %s (err %d)\n", version(&err), err); }
        if (def) { err = 0; len = def(buf, ARRAYSIZE(buf), &err); printf("icu default zone: length %d, err %d\n", len, err); }
        if (host) { err = 0; len = host(buf, ARRAYSIZE(buf), &err); printf("icu host zone: length %d, err %d\n", len, err); }
    }
    return 0;
}
