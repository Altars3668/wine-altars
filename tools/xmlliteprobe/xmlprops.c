/*
 * xmlprops - which reader and writer properties Windows' xmllite knows beyond the documented ones.
 *
 * Office sets reader property 18, which the SDK does not declare (the enum ends at 7); Wine answers it with a
 * FIXME and E_NOTIMPL.  This prints, for properties 0..31 of IXmlReader and IXmlWriter, what GetProperty answers
 * and returns on a fresh object, then what SetProperty answers for 0, 1 and 2 for the undeclared ones and
 * what GetProperty says afterwards.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <xmllite.h>
#include <stdio.h>

int main(void)
{
    IXmlReader *reader = NULL;
    IXmlWriter *writer = NULL;
    unsigned int i, v;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = CreateXmlReader(&IID_IXmlReader, (void **)&reader, NULL);
    printf("CreateXmlReader %#lx\n", hr);
    hr = CreateXmlWriter(&IID_IXmlWriter, (void **)&writer, NULL);
    printf("CreateXmlWriter %#lx\n", hr);
    for (i = 0; reader && i < 32; i++)
    {
        LONG_PTR value = 0x5a5a;
        hr = IXmlReader_GetProperty(reader, i, &value);
        printf("reader get %u: %#lx value %#Ix\n", i, hr, value);
    }
    for (i = 0; writer && i < 32; i++)
    {
        LONG_PTR value = 0x5a5a;
        hr = IXmlWriter_GetProperty(writer, i, &value);
        printf("writer get %u: %#lx value %#Ix\n", i, hr, value);
    }
    for (i = 8; reader && i < 32; i++)
        for (v = 0; v < 3; v++)
        {
            LONG_PTR value = 0x5a5a;
            HRESULT set = IXmlReader_SetProperty(reader, i, v);
            hr = IXmlReader_GetProperty(reader, i, &value);
            if (set != E_INVALIDARG || hr != E_INVALIDARG)
                printf("reader set %u to %u: %#lx, then get %#lx value %#Ix\n", i, v, set, hr, value);
        }
    for (i = 4; writer && i < 32; i++)
        for (v = 0; v < 3; v++)
        {
            LONG_PTR value = 0x5a5a;
            HRESULT set = IXmlWriter_SetProperty(writer, i, v);
            hr = IXmlWriter_GetProperty(writer, i, &value);
            if (set != E_INVALIDARG || hr != E_INVALIDARG)
                printf("writer set %u to %u: %#lx, then get %#lx value %#Ix\n", i, v, set, hr, value);
        }
    if (reader) IXmlReader_Release(reader);
    if (writer) IXmlWriter_Release(writer);
    CoUninitialize();
    return 0;
}
