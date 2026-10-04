#include <windows.h>
#include <winioctl.h>
#include <stdio.h>

int main(void)
{
    STORAGE_PROPERTY_QUERY query;
    BYTE output[512];
    HANDLE device;
    DWORD i, property, length, returned;
    BOOL ret;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    device = CreateFileW(L"\\\\.\\C:", 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) return 1;
    for (i = 4; i <= 13; ++i)
    {
        memset(&query, 0, sizeof(query));
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        returned = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        ret = DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, i, output, sizeof(output), &returned, NULL);
        printf("input=%lu ok=%u error=%lu returned=%lu\n", i, ret, GetLastError(), returned);
    }
    {
        static const DWORD sizes[] = {0, 1, 7, 8, 9, 16, 39, 40, 41, 64, 128, 512};
        memset(&query, 0, sizeof(query));
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        for (i = 0; i < ARRAYSIZE(sizes); ++i)
        {
            returned = 0xdeadbeef;
            SetLastError(0xdeadbeef);
            ret = DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), output, sizes[i], &returned, NULL);
            printf("property=0 output=%lu ok=%u error=%lu returned=%lu\n", sizes[i], ret, GetLastError(), returned);
        }
    }
    for (property = StorageDeviceSeekPenaltyProperty; property <= StorageDeviceTrimProperty; ++property)
    {
        memset(&query, 0, sizeof(query));
        query.PropertyId = property;
        query.QueryType = PropertyStandardQuery;
        for (length = 0; length <= 13; ++length)
        {
            returned = 0xdeadbeef;
            SetLastError(0xdeadbeef);
            ret = DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), output, length, &returned, NULL);
            printf("property=%lu output=%lu ok=%u error=%lu returned=%lu\n", property, length, ret, GetLastError(), returned);
        }
    }
    CloseHandle(device);
    puts("done");
    return 0;
}
