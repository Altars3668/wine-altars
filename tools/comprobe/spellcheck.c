/*
 * The Windows spell checking API (MsSpellCheckingFacility.dll, Windows 8 and later), which Office asks for and
 * Wine does not have: which languages it offers, what a checker says about a sentence with mistakes in it, what it
 * suggests, its options, and what it does with words added, ignored, and a language it does not have.  The
 * interfaces are declared here from the SDK; each is asked for by IID, which checks those too.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

typedef enum { CORRECTIVE_ACTION_NONE, CORRECTIVE_ACTION_GET_SUGGESTIONS, CORRECTIVE_ACTION_REPLACE,
               CORRECTIVE_ACTION_DELETE } CORRECTIVE_ACTION;

typedef struct ISpellingError ISpellingError;
typedef struct { HRESULT (WINAPI *QueryInterface)(ISpellingError *, REFIID, void **);
                 ULONG (WINAPI *AddRef)(ISpellingError *); ULONG (WINAPI *Release)(ISpellingError *);
                 HRESULT (WINAPI *get_StartIndex)(ISpellingError *, ULONG *);
                 HRESULT (WINAPI *get_Length)(ISpellingError *, ULONG *);
                 HRESULT (WINAPI *get_CorrectiveAction)(ISpellingError *, CORRECTIVE_ACTION *);
                 HRESULT (WINAPI *get_Replacement)(ISpellingError *, WCHAR **); } ISpellingErrorVtbl;
struct ISpellingError { const ISpellingErrorVtbl *lpVtbl; };

typedef struct IEnumSpellingError IEnumSpellingError;
typedef struct { HRESULT (WINAPI *QueryInterface)(IEnumSpellingError *, REFIID, void **);
                 ULONG (WINAPI *AddRef)(IEnumSpellingError *); ULONG (WINAPI *Release)(IEnumSpellingError *);
                 HRESULT (WINAPI *Next)(IEnumSpellingError *, ISpellingError **); } IEnumSpellingErrorVtbl;
struct IEnumSpellingError { const IEnumSpellingErrorVtbl *lpVtbl; };

typedef struct IOptionDescription IOptionDescription;
typedef struct { HRESULT (WINAPI *QueryInterface)(IOptionDescription *, REFIID, void **);
                 ULONG (WINAPI *AddRef)(IOptionDescription *); ULONG (WINAPI *Release)(IOptionDescription *);
                 HRESULT (WINAPI *get_Id)(IOptionDescription *, WCHAR **);
                 HRESULT (WINAPI *get_Heading)(IOptionDescription *, WCHAR **);
                 HRESULT (WINAPI *get_Description)(IOptionDescription *, WCHAR **);
                 HRESULT (WINAPI *get_Labels)(IOptionDescription *, IEnumString **); } IOptionDescriptionVtbl;
struct IOptionDescription { const IOptionDescriptionVtbl *lpVtbl; };

typedef struct ISpellChecker ISpellChecker;
typedef struct { HRESULT (WINAPI *QueryInterface)(ISpellChecker *, REFIID, void **);
                 ULONG (WINAPI *AddRef)(ISpellChecker *); ULONG (WINAPI *Release)(ISpellChecker *);
                 HRESULT (WINAPI *get_LanguageTag)(ISpellChecker *, WCHAR **);
                 HRESULT (WINAPI *Check)(ISpellChecker *, const WCHAR *, IEnumSpellingError **);
                 HRESULT (WINAPI *Suggest)(ISpellChecker *, const WCHAR *, IEnumString **);
                 HRESULT (WINAPI *Add)(ISpellChecker *, const WCHAR *);
                 HRESULT (WINAPI *Ignore)(ISpellChecker *, const WCHAR *);
                 HRESULT (WINAPI *AutoCorrect)(ISpellChecker *, const WCHAR *, const WCHAR *);
                 HRESULT (WINAPI *GetOptionValue)(ISpellChecker *, const WCHAR *, BYTE *);
                 HRESULT (WINAPI *get_OptionIds)(ISpellChecker *, IEnumString **);
                 HRESULT (WINAPI *get_Id)(ISpellChecker *, WCHAR **);
                 HRESULT (WINAPI *get_LocalizedName)(ISpellChecker *, WCHAR **);
                 HRESULT (WINAPI *add_SpellCheckerChanged)(ISpellChecker *, IUnknown *, DWORD *);
                 HRESULT (WINAPI *remove_SpellCheckerChanged)(ISpellChecker *, DWORD);
                 HRESULT (WINAPI *GetOptionDescription)(ISpellChecker *, const WCHAR *, IOptionDescription **);
                 HRESULT (WINAPI *ComprehensiveCheck)(ISpellChecker *, const WCHAR *, IEnumSpellingError **);
                 HRESULT (WINAPI *Remove)(ISpellChecker *, const WCHAR *); } ISpellCheckerVtbl;
struct ISpellChecker { const ISpellCheckerVtbl *lpVtbl; };

typedef struct ISpellCheckerFactory ISpellCheckerFactory;
typedef struct { HRESULT (WINAPI *QueryInterface)(ISpellCheckerFactory *, REFIID, void **);
                 ULONG (WINAPI *AddRef)(ISpellCheckerFactory *); ULONG (WINAPI *Release)(ISpellCheckerFactory *);
                 HRESULT (WINAPI *get_SupportedLanguages)(ISpellCheckerFactory *, IEnumString **);
                 HRESULT (WINAPI *IsSupported)(ISpellCheckerFactory *, const WCHAR *, BOOL *);
                 HRESULT (WINAPI *CreateSpellChecker)(ISpellCheckerFactory *, const WCHAR *, ISpellChecker **);
               } ISpellCheckerFactoryVtbl;
struct ISpellCheckerFactory { const ISpellCheckerFactoryVtbl *lpVtbl; };

static const CLSID CLSID_SpellCheckerFactory =
        {0x7ab36653, 0x1796, 0x484b, {0xbd, 0xfa, 0xe7, 0x4f, 0x1d, 0xb7, 0xc1, 0xdc}};
static const IID IID_ISpellCheckerFactory =
        {0x8e018a9d, 0x2415, 0x4677, {0xbf, 0x08, 0x79, 0x4e, 0xa6, 0x1f, 0x94, 0xbb}};
static const IID IID_IUserDictionariesRegistrar =
        {0xaa176b85, 0x0e12, 0x4844, {0x8e, 0x1a, 0xee, 0xf1, 0xda, 0x77, 0xf5, 0x86}};
static const IID IID_ISpellChecker =
        {0xb6fd0b71, 0xe2bc, 0x4653, {0x8d, 0x05, 0xf1, 0x97, 0xe4, 0x12, 0x77, 0x0b}};
static const IID IID_ISpellChecker2 =
        {0xe7ed1c71, 0x87f7, 0x4378, {0xa8, 0x40, 0xc9, 0x20, 0x0d, 0xac, 0xee, 0x47}};
static const IID IID_IEnumSpellingError =
        {0x803e3bd4, 0x2828, 0x4410, {0x82, 0x90, 0x41, 0x8d, 0x1d, 0x73, 0xc7, 0x62}};
static const IID IID_ISpellingError =
        {0xb7c82d61, 0xfbe8, 0x4b47, {0x9b, 0x27, 0x6c, 0x0d, 0x2e, 0x0d, 0xe0, 0xa3}};

static void print_strings(const char *name, HRESULT hr, IEnumString *strings, unsigned int max)
{
    unsigned int count = 0;
    WCHAR *s;

    printf("%s: hr %#lx:", name, hr);
    if (SUCCEEDED(hr) && strings)
    {
        while (IEnumString_Next(strings, 1, &s, NULL) == S_OK)
        {
            if (count++ < max)
                printf(" \"%ls\"", s);
            CoTaskMemFree(s);
        }
        printf(" (%u)", count);
        IEnumString_Release(strings);
    }
    printf("\n");
}

static void check(ISpellChecker *checker, const WCHAR *text, BOOL comprehensive)
{
    IEnumSpellingError *errors;
    ISpellingError *error;
    CORRECTIVE_ACTION action;
    ULONG start, length;
    WCHAR *replacement;
    void *unk;
    HRESULT hr;

    hr = comprehensive ? checker->lpVtbl->ComprehensiveCheck(checker, text, &errors)
            : checker->lpVtbl->Check(checker, text, &errors);
    printf("%s \"%ls\": hr %#lx\n", comprehensive ? "ComprehensiveCheck" : "Check", text, hr);
    if (FAILED(hr))
        return;
    printf("  enum QI %#lx\n", errors->lpVtbl->QueryInterface(errors, &IID_IEnumSpellingError, &unk));
    if (unk) ((IUnknown *)unk)->lpVtbl->Release(unk);
    while ((hr = errors->lpVtbl->Next(errors, &error)) == S_OK)
    {
        unk = NULL;
        printf("  error QI %#lx\n", error->lpVtbl->QueryInterface(error, &IID_ISpellingError, &unk));
        if (unk) ((IUnknown *)unk)->lpVtbl->Release(unk);
        error->lpVtbl->get_StartIndex(error, &start);
        error->lpVtbl->get_Length(error, &length);
        error->lpVtbl->get_CorrectiveAction(error, &action);
        replacement = NULL;
        error->lpVtbl->get_Replacement(error, &replacement);
        printf("  error at %lu, length %lu, action %d, replacement \"%ls\"\n", start, length, action,
                replacement ? replacement : L"(null)");
        CoTaskMemFree(replacement);
        error->lpVtbl->Release(error);
    }
    printf("  Next: %#lx\n", hr);
    errors->lpVtbl->Release(errors);
}

int main(void)
{
    static const WCHAR *const languages[] = {L"en-US", L"en-us", L"en", L"en-GB", L"zh-CN", L"de-DE", L"fr-FR",
            L"xx-XX", L""};
    ISpellCheckerFactory *factory;
    IEnumString *strings;
    ISpellChecker *checker;
    WCHAR *s;
    unsigned int i;
    void *unk;
    BYTE value;
    BOOL supported;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr = CoCreateInstance(&CLSID_SpellCheckerFactory, NULL, CLSCTX_INPROC_SERVER,
            &IID_ISpellCheckerFactory, (void **)&factory)))
    {
        printf("CoCreateInstance %#lx\n", hr);
        return 1;
    }
    printf("IUserDictionariesRegistrar: %#lx\n",
            factory->lpVtbl->QueryInterface(factory, &IID_IUserDictionariesRegistrar, &unk));
    if (unk) ((IUnknown *)unk)->lpVtbl->Release(unk);

    hr = factory->lpVtbl->get_SupportedLanguages(factory, &strings);
    print_strings("SupportedLanguages", hr, strings, 50);
    for (i = 0; i < ARRAY_SIZE(languages); ++i)
    {
        supported = 7;
        hr = factory->lpVtbl->IsSupported(factory, languages[i], &supported);
        printf("IsSupported \"%ls\": hr %#lx, %d\n", languages[i], hr, supported);
    }
    hr = factory->lpVtbl->CreateSpellChecker(factory, L"xx-XX", &checker);
    printf("CreateSpellChecker xx-XX: %#lx\n", hr);
    if (SUCCEEDED(hr)) checker->lpVtbl->Release(checker);

    if (FAILED(hr = factory->lpVtbl->CreateSpellChecker(factory, L"en-US", &checker)))
    {
        printf("CreateSpellChecker en-US: %#lx\n", hr);
        return 1;
    }
    printf("ISpellChecker QI %#lx, ISpellChecker2 QI %#lx\n",
            checker->lpVtbl->QueryInterface(checker, &IID_ISpellChecker, &unk),
            checker->lpVtbl->QueryInterface(checker, &IID_ISpellChecker2, &unk));
    s = NULL;
    hr = checker->lpVtbl->get_LanguageTag(checker, &s);
    printf("LanguageTag: %#lx \"%ls\"\n", hr, s ? s : L"(null)");
    CoTaskMemFree(s);
    s = NULL;
    hr = checker->lpVtbl->get_Id(checker, &s);
    printf("Id: %#lx \"%ls\"\n", hr, s ? s : L"(null)");
    CoTaskMemFree(s);
    s = NULL;
    hr = checker->lpVtbl->get_LocalizedName(checker, &s);
    printf("LocalizedName: %#lx \"%ls\"\n", hr, s ? s : L"(null)");
    CoTaskMemFree(s);
    hr = checker->lpVtbl->get_OptionIds(checker, &strings);
    print_strings("OptionIds", hr, strings, 20);
    value = 0xcc;
    hr = checker->lpVtbl->GetOptionValue(checker, L"en-US:no_suggestions", &value);
    printf("GetOptionValue: %#lx %u\n", hr, value);

    check(checker, L"Helo wrld, this is a tset of teh spell checker.", FALSE);
    check(checker, L"This sentence is spelled correctly.", FALSE);
    check(checker, L"the the cat", FALSE);
    check(checker, L"Helo wrld, the the cat.", TRUE);
    check(checker, L"", FALSE);
    hr = checker->lpVtbl->Suggest(checker, L"helo", &strings);
    print_strings("Suggest helo", hr, strings, 8);
    hr = checker->lpVtbl->Suggest(checker, L"hello", &strings);
    print_strings("Suggest hello", hr, strings, 8);
    hr = checker->lpVtbl->Suggest(checker, L"xqzvkw", &strings);
    print_strings("Suggest xqzvkw", hr, strings, 8);
    hr = checker->lpVtbl->Ignore(checker, L"wrld");
    printf("Ignore: %#lx\n", hr);
    check(checker, L"Helo wrld", FALSE);

    checker->lpVtbl->Release(checker);
    factory->lpVtbl->Release(factory);
    CoUninitialize();
    printf("done\n");
    return 0;
}
