/* spellprobe: what the Windows spell checking API (MsSpellCheckingFacility) says -- the languages it has, the
 * errors and suggestions it gives for a set of texts in English, what adding, ignoring, removing and autocorrecting
 * words does and whether another checker sees it, its options, its change event and a registered user dictionary.
 *
 * Adding and autocorrecting write the current user's dictionaries (%APPDATA%\Microsoft\Spelling\<language>\
 * default.dic, .exc and .acl): they are read before and written back as they were at the end, and of what is in
 * them only the lines with the probe's own words are printed.  Text is printed with non-ASCII as \uXXXX.
 *
 *   spellprobe.exe [language]      the language checked, en-US by default
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <spellcheck.h>
#include <stdio.h>

static WCHAR lang[LOCALE_NAME_MAX_LENGTH] = L"en-US";

static void print_w(const WCHAR *str)
{
    if (!str)
    {
        printf("(null)");
        return;
    }
    for (; *str; str++)
    {
        if (*str == '\n') printf("\\n");
        else if (*str == '\r') printf("\\r");
        else if (*str == '\t') printf("\\t");
        else if (*str >= 0x20 && *str < 0x7f) printf("%c", (char)*str);
        else printf("\\u%04x", *str);
    }
}

static void print_strings(IEnumString *strings)
{
    LPOLESTR str;
    ULONG count = 0;

    if (!strings)
    {
        printf(" (none)");
        return;
    }
    while (IEnumString_Next(strings, 1, &str, NULL) == S_OK)
    {
        if (count++ < 12)
        {
            printf(" [");
            print_w(str);
            printf("]");
        }
        CoTaskMemFree(str);
    }
    if (count > 12) printf(" ... %lu in all", count);
    if (!count) printf(" (empty)");
}

/* the user's dictionaries, kept to be put back */
static const WCHAR *dict_exts[] = { L"dic", L"exc", L"acl" };
static BYTE *dict_data[3];
static DWORD dict_size[3];
static BOOL dict_exists[3];

static void dict_path(unsigned int i, WCHAR *path)
{
    WCHAR appdata[MAX_PATH];

    GetEnvironmentVariableW(L"APPDATA", appdata, ARRAYSIZE(appdata));
    swprintf(path, MAX_PATH, L"%ls\\Microsoft\\Spelling\\%ls\\default.%ls", appdata, lang, dict_exts[i]);
}

static BYTE *read_file(const WCHAR *path, DWORD *size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, 0, NULL);
    BYTE *data;

    if (file == INVALID_HANDLE_VALUE) return NULL;
    *size = GetFileSize(file, NULL);
    data = malloc(*size + 2);
    ReadFile(file, data, *size, size, NULL);
    data[*size] = data[*size + 1] = 0;
    CloseHandle(file);
    return data;
}

static void save_dictionaries(void)
{
    WCHAR path[MAX_PATH];
    unsigned int i;

    for (i = 0; i < 3; i++)
    {
        dict_path(i, path);
        dict_data[i] = read_file(path, &dict_size[i]);
        dict_exists[i] = dict_data[i] != NULL;
        printf("default.%ls %s", dict_exts[i], dict_exists[i] ? "exists" : "does not exist");
        if (dict_exists[i]) printf(", %lu bytes", dict_size[i]);
        printf("\n");
    }
}

/* the lines of a dictionary with the probe's words, and what the file starts with */
static void show_dictionaries(const char *when)
{
    WCHAR path[MAX_PATH], *text, *line, *end;
    unsigned int i;
    BYTE *data;
    DWORD size;

    printf("dictionaries %s\n", when);
    for (i = 0; i < 3; i++)
    {
        dict_path(i, path);
        if (!(data = read_file(path, &size)))
        {
            printf("  default.%ls: none\n", dict_exts[i]);
            continue;
        }
        printf("  default.%ls: %lu bytes, starts %02x %02x\n", dict_exts[i], size, size > 0 ? data[0] : 0,
               size > 1 ? data[1] : 0);
        text = (WCHAR *)data;
        if (size >= 2 && data[0] == 0xff && data[1] == 0xfe) text++;
        for (line = text; *line; line = end)
        {
            for (end = line; *end && *end != '\n'; end++) ;
            if (*end) end++;
            if (wcsstr(line, L"wineprobe") && wcsstr(line, L"wineprobe") < end)
            {
                WCHAR copy[256];
                lstrcpynW(copy, line, min(ARRAYSIZE(copy), end - line + 1));
                printf("    [");
                print_w(copy);
                printf("]\n");
            }
        }
        free(data);
    }
}

static void restore_dictionaries(void)
{
    WCHAR path[MAX_PATH];
    unsigned int i;
    HANDLE file;
    DWORD written;

    for (i = 0; i < 3; i++)
    {
        dict_path(i, path);
        if (!dict_exists[i])
        {
            DeleteFileW(path);
            continue;
        }
        file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        WriteFile(file, dict_data[i], dict_size[i], &written, NULL);
        CloseHandle(file);
        free(dict_data[i]);
    }
}

static void check(ISpellChecker *checker, const WCHAR *text, BOOL comprehensive)
{
    IEnumSpellingError *errors = NULL;
    ISpellingError *error;
    CORRECTIVE_ACTION action;
    ULONG start, length;
    LPWSTR replacement;
    HRESULT hr;

    printf("  %s [", comprehensive ? "ComprehensiveCheck" : "Check");
    print_w(text);
    if (comprehensive) hr = ISpellChecker_ComprehensiveCheck(checker, text, &errors);
    else hr = ISpellChecker_Check(checker, text, &errors);
    printf("]: %#lx", hr);
    if (FAILED(hr) || !errors)
    {
        printf("\n");
        return;
    }
    while ((hr = IEnumSpellingError_Next(errors, &error)) == S_OK)
    {
        ISpellingError_get_StartIndex(error, &start);
        ISpellingError_get_Length(error, &length);
        ISpellingError_get_CorrectiveAction(error, &action);
        replacement = NULL;
        ISpellingError_get_Replacement(error, &replacement);
        printf(" {%lu,%lu,%d,", start, length, action);
        print_w(replacement);
        printf("}");
        CoTaskMemFree(replacement);
        ISpellingError_Release(error);
    }
    printf(" end %#lx\n", hr);
    IEnumSpellingError_Release(errors);
}

static void suggest(ISpellChecker *checker, const WCHAR *word)
{
    IEnumString *strings = NULL;
    HRESULT hr;

    printf("  Suggest [");
    print_w(word);
    hr = ISpellChecker_Suggest(checker, word, &strings);
    printf("]: %#lx", hr);
    if (SUCCEEDED(hr)) print_strings(strings);
    printf("\n");
    if (strings) IEnumString_Release(strings);
}

static LONG events;

static HRESULT WINAPI handler_QueryInterface(ISpellCheckerChangedEventHandler *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_ISpellCheckerChangedEventHandler))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_AddRef(ISpellCheckerChangedEventHandler *iface) { return 2; }
static ULONG WINAPI handler_Release(ISpellCheckerChangedEventHandler *iface) { return 1; }

static HRESULT WINAPI handler_Invoke(ISpellCheckerChangedEventHandler *iface, ISpellChecker *sender)
{
    InterlockedIncrement(&events);
    return S_OK;
}

static ISpellCheckerChangedEventHandlerVtbl handler_vtbl =
{
    handler_QueryInterface, handler_AddRef, handler_Release, handler_Invoke,
};
static ISpellCheckerChangedEventHandler handler = { &handler_vtbl };

int wmain(int argc, WCHAR **argv)
{
    static const WCHAR *tags[] = { L"en-US", L"en-us", L"EN-US", L"en", L"en-GB", L"en_US", L"fr-FR", L"de-DE",
                                   L"zh-CN", L"xx-XX", L"", L" en-US" };
    static const WCHAR *texts[] =
    {
        L"hello world", L"helo wrold", L"the the cat", L"Hello, world. hello", L"teh", L"dont don't", L"e-mail co-operate",
        L"1234 abc123 3rd", L"IBM WORLD Wrold", L"http://example.com/foo user@example.com", L"", L"   ",
        L"na\x00efve caf\x00e9", L"Hello\nworld", L"x a I i", L"Mr. Smith U.S.A. OK", L"supercalifragilisticexpialidocious",
        L"wineprobeword", L"hello  world", L"Hello. the end. The end", L"it's its", L"HELLO wORLD",
        L"a a", L"The the", L"the The", L"the, the", L"the  the", L"the\nthe", L"hello hello hello", L"Wrold's",
        L"helo-world", L"wrold.", L"(wrold)", L"\"wrold\"", L"wrold\x2019s", L"\x4f60\x597d wrold"
    };
    static const WCHAR *words[] = { L"helo", L"wrold", L"teh", L"hello", L"Helo", L"HELO", L"zzzzqqqq", L"", L"two words" };
    IEnumSpellingError *errors;
    ISpellCheckerFactory *factory;
    IUserDictionariesRegistrar *registrar;
    ISpellChecker *checker, *other;
    ISpellChecker2 *checker2;
    IOptionDescription *description;
    IEnumString *strings;
    WCHAR path[MAX_PATH];
    LPOLESTR str;
    unsigned int i;
    DWORD token;
    BOOL supported;
    HANDLE file;
    DWORD written;
    BYTE value;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1) lstrcpynW(lang, argv[1], ARRAYSIZE(lang));
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    hr = CoCreateInstance(&CLSID_SpellCheckerFactory, NULL, CLSCTX_INPROC_SERVER, &IID_ISpellCheckerFactory,
                          (void **)&factory);
    printf("CoCreateInstance: %#lx\n", hr);
    if (FAILED(hr)) return 1;
    hr = ISpellCheckerFactory_QueryInterface(factory, &IID_IUserDictionariesRegistrar, (void **)&registrar);
    printf("IUserDictionariesRegistrar: %#lx\n", hr);

    strings = NULL;
    hr = ISpellCheckerFactory_get_SupportedLanguages(factory, &strings);
    printf("SupportedLanguages: %#lx", hr);
    print_strings(strings);
    printf("\n");
    if (strings) IEnumString_Release(strings);

    for (i = 0; i < ARRAYSIZE(tags); i++)
    {
        supported = 0xdead;
        hr = ISpellCheckerFactory_IsSupported(factory, tags[i], &supported);
        printf("IsSupported [%ls]: %#lx %d", tags[i], hr, supported);
        checker = NULL;
        hr = ISpellCheckerFactory_CreateSpellChecker(factory, tags[i], &checker);
        printf(", CreateSpellChecker %#lx %p\n", hr, checker ? (void *)1 : NULL);
        if (checker) ISpellChecker_Release(checker);
    }
    hr = ISpellCheckerFactory_IsSupported(factory, NULL, &supported);
    printf("IsSupported NULL: %#lx\n", hr);
    hr = ISpellCheckerFactory_CreateSpellChecker(factory, NULL, &checker);
    printf("CreateSpellChecker NULL: %#lx\n", hr);

    hr = ISpellCheckerFactory_CreateSpellChecker(factory, L"EN-us", &checker);
    if (SUCCEEDED(hr))
    {
        str = NULL;
        ISpellChecker_get_LanguageTag(checker, &str);
        printf("CreateSpellChecker [EN-us]: LanguageTag [");
        print_w(str);
        printf("]\n");
        CoTaskMemFree(str);
        ISpellChecker_Release(checker);
    }
    hr = ISpellCheckerFactory_CreateSpellChecker(factory, lang, &checker);
    printf("CreateSpellChecker [%ls]: %#lx\n", lang, hr);
    if (FAILED(hr)) return 1;
    str = NULL;
    hr = ISpellChecker_get_LanguageTag(checker, &str);
    printf("  LanguageTag: %#lx [", hr);
    print_w(str);
    printf("]\n");
    CoTaskMemFree(str);
    str = NULL;
    hr = ISpellChecker_get_Id(checker, &str);
    printf("  Id: %#lx [", hr);
    print_w(str);
    printf("]\n");
    CoTaskMemFree(str);
    str = NULL;
    hr = ISpellChecker_get_LocalizedName(checker, &str);
    printf("  LocalizedName: %#lx [", hr);
    print_w(str);
    printf("]\n");
    CoTaskMemFree(str);

    strings = NULL;
    hr = ISpellChecker_get_OptionIds(checker, &strings);
    printf("  OptionIds: %#lx", hr);
    print_strings(strings);
    printf("\n");
    if (strings)
    {
        IEnumString_Reset(strings);
        while (IEnumString_Next(strings, 1, &str, NULL) == S_OK)
        {
            IEnumString *labels = NULL;
            LPWSTR id = NULL, heading = NULL, text = NULL;

            value = 0xcc;
            hr = ISpellChecker_GetOptionValue(checker, str, &value);
            printf("  option [");
            print_w(str);
            printf("]: value %#lx %u", hr, value);
            hr = ISpellChecker_GetOptionDescription(checker, str, &description);
            printf(", description %#lx", hr);
            if (SUCCEEDED(hr))
            {
                IOptionDescription_get_Id(description, &id);
                IOptionDescription_get_Heading(description, &heading);
                IOptionDescription_get_Description(description, &text);
                IOptionDescription_get_Labels(description, &labels);
                printf(" id [");
                print_w(id);
                printf("] heading [");
                print_w(heading);
                printf("] description [");
                print_w(text);
                printf("] labels");
                print_strings(labels);
                if (labels) IEnumString_Release(labels);
                CoTaskMemFree(id);
                CoTaskMemFree(heading);
                CoTaskMemFree(text);
                IOptionDescription_Release(description);
            }
            printf("\n");
            CoTaskMemFree(str);
        }
        IEnumString_Release(strings);
    }
    value = 0xcc;
    hr = ISpellChecker_GetOptionValue(checker, L"no:such:option", &value);
    printf("  GetOptionValue no:such:option: %#lx %u\n", hr, value);
    hr = ISpellChecker_GetOptionDescription(checker, L"no:such:option", &description);
    printf("  GetOptionDescription no:such:option: %#lx\n", hr);

    printf("checks\n");
    for (i = 0; i < ARRAYSIZE(texts); i++)
    {
        check(checker, texts[i], FALSE);
        check(checker, texts[i], TRUE);
    }
    errors = NULL;
    hr = ISpellChecker_Check(checker, NULL, &errors);
    printf("  Check NULL: %#lx %p\n", hr, errors);
    if (errors) IEnumSpellingError_Release(errors);
    printf("suggestions\n");
    for (i = 0; i < ARRAYSIZE(words); i++) suggest(checker, words[i]);
    strings = NULL;
    hr = ISpellChecker_Suggest(checker, NULL, &strings);
    printf("  Suggest NULL: %#lx %p\n", hr, strings);
    if (strings) IEnumString_Release(strings);

    printf("words\n");
    save_dictionaries();
    hr = ISpellCheckerFactory_CreateSpellChecker(factory, lang, &other);
    printf("  another checker: %#lx\n", hr);
    hr = ISpellChecker_add_SpellCheckerChanged(checker, &handler, &token);
    printf("  add_SpellCheckerChanged: %#lx\n", hr);

    hr = ISpellChecker_Add(checker, L"wineprobeword");
    printf("  Add wineprobeword: %#lx\n", hr);
    Sleep(500);
    printf("  events %ld\n", events);
    check(checker, L"wineprobeword Wineprobeword WINEPROBEWORD", FALSE);
    check(other, L"wineprobeword", FALSE);
    suggest(checker, L"wineprobewrd");
    show_dictionaries("after Add");

    hr = ISpellChecker_Ignore(checker, L"wineprobeignored");
    printf("  Ignore wineprobeignored: %#lx\n", hr);
    Sleep(500);
    printf("  events %ld\n", events);
    check(checker, L"wineprobeignored Wineprobeignored", FALSE);
    check(other, L"wineprobeignored", FALSE);

    hr = ISpellChecker_QueryInterface(checker, &IID_ISpellChecker2, (void **)&checker2);
    printf("  ISpellChecker2: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = ISpellChecker2_Remove(checker2, L"wineprobeword");
        printf("  Remove wineprobeword: %#lx\n", hr);
        Sleep(500);
        printf("  events %ld\n", events);
        check(checker, L"wineprobeword", FALSE);
        check(other, L"wineprobeword", FALSE);
        hr = ISpellChecker2_Remove(checker2, L"hello");
        printf("  Remove hello: %#lx\n", hr);
        Sleep(500);
        check(checker, L"hello", FALSE);
        show_dictionaries("after Remove");
        hr = ISpellChecker_Add(checker, L"hello");
        printf("  Add hello: %#lx\n", hr);
        check(checker, L"hello", FALSE);
        show_dictionaries("after Add hello");
        ISpellChecker2_Release(checker2);
    }
    hr = ISpellChecker_remove_SpellCheckerChanged(checker, token);
    printf("  remove_SpellCheckerChanged: %#lx\n", hr);

    if (registrar)
    {
        swprintf(path, ARRAYSIZE(path), L"%ls", L"C:\\WineAltarsTest");
        CreateDirectoryW(path, NULL);
        wcscat(path, L"\\wineprobe.dic");
        file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        WriteFile(file, "\xff\xfew\0i\0n\0e\0p\0r\0o\0b\0e\0u\0s\0e\0r\0\r\0\n\0", 30, &written, NULL);
        CloseHandle(file);
        hr = IUserDictionariesRegistrar_RegisterUserDictionary(registrar, path, lang);
        printf("  RegisterUserDictionary: %#lx\n", hr);
        Sleep(500);
        check(checker, L"wineprobeuser", FALSE);
        hr = IUserDictionariesRegistrar_RegisterUserDictionary(registrar, path, lang);
        printf("  RegisterUserDictionary again: %#lx\n", hr);
        hr = IUserDictionariesRegistrar_UnregisterUserDictionary(registrar, path, lang);
        printf("  UnregisterUserDictionary: %#lx\n", hr);
        Sleep(500);
        check(checker, L"wineprobeuser", FALSE);
        hr = IUserDictionariesRegistrar_UnregisterUserDictionary(registrar, path, lang);
        printf("  UnregisterUserDictionary again: %#lx\n", hr);
        hr = IUserDictionariesRegistrar_RegisterUserDictionary(registrar, L"C:\\WineAltarsTest\\none.dic", lang);
        printf("  RegisterUserDictionary missing file: %#lx\n", hr);
        hr = IUserDictionariesRegistrar_RegisterUserDictionary(registrar, L"C:\\WineAltarsTest\\wineprobe.txt", lang);
        printf("  RegisterUserDictionary .txt: %#lx\n", hr);
        DeleteFileW(path);
        IUserDictionariesRegistrar_Release(registrar);
    }

    Sleep(2000);
    show_dictionaries("two seconds on");

    hr = ISpellChecker_AutoCorrect(checker, L"wineprobeac", L"wineprobereplacement");
    printf("  AutoCorrect wineprobeac: %#lx\n", hr);
    Sleep(500);
    printf("  events %ld\n", events);
    check(checker, L"wineprobeac Wineprobeac WINEPROBEAC", FALSE);
    check(checker, L"wineprobeac", TRUE);
    check(other, L"wineprobeac", FALSE);
    suggest(checker, L"wineprobeac");
    show_dictionaries("after AutoCorrect");
    hr = ISpellChecker_AutoCorrect(checker, L"wineprobeac", L"wineprobeother");
    printf("  AutoCorrect wineprobeac again: %#lx\n", hr);
    check(checker, L"wineprobeac", FALSE);
    hr = ISpellChecker_AutoCorrect(checker, L"", L"x");
    printf("  AutoCorrect empty: %#lx\n", hr);
    hr = ISpellChecker_AutoCorrect(checker, L"wineprobeac2", L"");
    printf("  AutoCorrect to empty: %#lx\n", hr);
    hr = ISpellChecker_AutoCorrect(checker, L"wineprobe two", L"x");
    printf("  AutoCorrect from two words: %#lx\n", hr);
    check(checker, L"hello", FALSE);
    hr = ISpellChecker_Add(checker, L"two words");
    printf("  Add two words: %#lx\n", hr);
    hr = ISpellChecker_Add(checker, L"");
    printf("  Add empty: %#lx\n", hr);
    check(checker, L"hello", FALSE);
    hr = ISpellChecker_AutoCorrect(checker, L"wineprobeac3", L"wineprobe replacement");
    printf("  AutoCorrect to two words: %#lx\n", hr);
    Sleep(500);
    check(checker, L"hello", FALSE);
    check(checker, L"wineprobeac3", TRUE);
    show_dictionaries("at the end");

    ISpellChecker_Release(other);
    ISpellChecker_Release(checker);
    ISpellCheckerFactory_Release(factory);
    restore_dictionaries();
    show_dictionaries("restored");
    CoUninitialize();
    return 0;
}
