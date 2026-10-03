#include "probe.h"
#include <winioctl.h>
#include <stddef.h>

static void descriptor_string(const char *label, const BYTE *buffer, DWORD length,
                              DWORD offset, BOOL serial)
{
    const BYTE *end;
    size_t count, i;
    if (!offset)
    {
        printf(" %s_length=0 absent=1", label);
        return;
    }
    if (offset >= length)
    {
        printf(" %s_offset_out_of_range=1", label);
        return;
    }
    end = memchr(buffer + offset, 0, length - offset);
    count = end ? (size_t)(end - buffer - offset) : length - offset;
    printf(" %s_length=%llu terminated=%u", label, (unsigned long long)count, !!end);
    if (serial) return;
    printf(" %s=\"", label);
    for (i = 0; i < count; ++i)
    {
        BYTE c = buffer[offset + i];
        if (c >= 32 && c <= 126 && c != '"' && c != '\\') putchar(c);
        else printf("\\x%02x", c);
    }
    putchar('"');
}

static DWORD query(HANDLE device, const char *label, STORAGE_PROPERTY_ID property,
                   STORAGE_QUERY_TYPE type, DWORD input_length, DWORD output_length,
                   BYTE *buffer, DWORD allocation)
{
    STORAGE_PROPERTY_QUERY request;
    DWORD returned = PROBE_SENTINEL, error;
    BOOL ok;
    STORAGE_DESCRIPTOR_HEADER header;
    STORAGE_DEVICE_DESCRIPTOR desc;
    DEVICE_SEEK_PENALTY_DESCRIPTOR seek;
    DWORD usable;

    memset(&request, 0, sizeof(request));
    request.PropertyId = property;
    request.QueryType = type;
    memset(buffer, 0xa5, allocation);
    SetLastError(PROBE_SENTINEL);
    ok = DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &request, input_length,
                         buffer, output_length, &returned, NULL);
    error = GetLastError();
    printf("query=%s property=%u type=%u input=%lu output=%lu ok=%u gle=%lu returned=%lu",
           label, property, type, input_length, output_length, ok, error, returned);
    usable = returned <= output_length ? returned : 0;
    if (type == PropertyStandardQuery && usable >= sizeof(header))
    {
        memcpy(&header, buffer, sizeof(header));
        printf(" Version=%lu Size=%lu", header.Version, header.Size);
    }
    else memset(&header, 0, sizeof(header));
    if (property == StorageDeviceProperty && type == PropertyStandardQuery &&
        usable >= offsetof(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties))
    {
        memset(&desc, 0, sizeof(desc));
        memcpy(&desc, buffer, offsetof(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties));
        printf(" DeviceType=%u DeviceTypeModifier=%u RemovableMedia=%u CommandQueueing=%u"
               " VendorIdOffset=%lu ProductIdOffset=%lu ProductRevisionOffset=%lu"
               " SerialNumberOffset=%lu BusType=%u RawPropertiesLength=%lu",
               desc.DeviceType, desc.DeviceTypeModifier, desc.RemovableMedia, desc.CommandQueueing,
               desc.VendorIdOffset, desc.ProductIdOffset, desc.ProductRevisionOffset,
               desc.SerialNumberOffset, desc.BusType, desc.RawPropertiesLength);
        descriptor_string("Vendor", buffer, usable, desc.VendorIdOffset, FALSE);
        descriptor_string("Product", buffer, usable, desc.ProductIdOffset, FALSE);
        descriptor_string("Revision", buffer, usable, desc.ProductRevisionOffset, FALSE);
        descriptor_string("SerialNumber", buffer, usable, desc.SerialNumberOffset, TRUE);
    }
    if (property == StorageDeviceSeekPenaltyProperty && type == PropertyStandardQuery &&
        usable >= sizeof(seek))
    {
        memcpy(&seek, buffer, sizeof(seek));
        printf(" IncursSeekPenalty=%u", seek.IncursSeekPenalty);
    }
    puts("");
    return header.Size;
}

static void device_properties(const WCHAR *path, const char *label)
{
    HANDLE device;
    DWORD error, size, allocation = 65536;
    BYTE *buffer = malloc(allocation);
    if (!buffer)
    {
        puts("allocation_failed");
        return;
    }
    SetLastError(PROBE_SENTINEL);
    device = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         NULL, OPEN_EXISTING, 0, NULL);
    error = GetLastError();
    printf("device=%s open=%u gle=%lu desired_access=0\n", label,
           device != INVALID_HANDLE_VALUE, error);
    if (device == INVALID_HANDLE_VALUE)
    {
        free(buffer);
        return;
    }
    size = query(device, "device_header", StorageDeviceProperty, PropertyStandardQuery,
                 sizeof(STORAGE_PROPERTY_QUERY), 8, buffer, allocation);
    if (size > allocation && size <= 1024 * 1024)
    {
        BYTE *larger = realloc(buffer, size);
        if (larger) { buffer = larger; allocation = size; }
    }
    if (size > allocation) printf("reported_size_exceeds_safety_cap=%lu\n", size);
    query(device, "device_full", StorageDeviceProperty, PropertyStandardQuery,
          sizeof(STORAGE_PROPERTY_QUERY), allocation, buffer, allocation);
    query(device, "device_zero", StorageDeviceProperty, PropertyStandardQuery,
          sizeof(STORAGE_PROPERTY_QUERY), 0, buffer, allocation);
    query(device, "device_seven", StorageDeviceProperty, PropertyStandardQuery,
          sizeof(STORAGE_PROPERTY_QUERY), 7, buffer, allocation);
    query(device, "short_input", StorageDeviceProperty, PropertyStandardQuery,
          4, allocation, buffer, allocation);
    query(device, "device_exists", StorageDeviceProperty, PropertyExistsQuery,
          sizeof(STORAGE_PROPERTY_QUERY), 8, buffer, allocation);
    query(device, "device_exists_zero", StorageDeviceProperty, PropertyExistsQuery,
          sizeof(STORAGE_PROPERTY_QUERY), 0, buffer, allocation);
    query(device, "seek_header", StorageDeviceSeekPenaltyProperty, PropertyStandardQuery,
          sizeof(STORAGE_PROPERTY_QUERY), 8, buffer, allocation);
    query(device, "seek_full", StorageDeviceSeekPenaltyProperty, PropertyStandardQuery,
          sizeof(STORAGE_PROPERTY_QUERY), sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR), buffer, allocation);
    query(device, "seek_short", StorageDeviceSeekPenaltyProperty, PropertyStandardQuery,
          sizeof(STORAGE_PROPERTY_QUERY), sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR) - 1, buffer, allocation);
    query(device, "seek_exists", StorageDeviceSeekPenaltyProperty, PropertyExistsQuery,
          sizeof(STORAGE_PROPERTY_QUERY), 8, buffer, allocation);
    query(device, "invalid_query_type", StorageDeviceProperty, (STORAGE_QUERY_TYPE)0x7fffffff,
          sizeof(STORAGE_PROPERTY_QUERY), allocation, buffer, allocation);
    query(device, "invalid_property", (STORAGE_PROPERTY_ID)0x7fffffff, PropertyStandardQuery,
          sizeof(STORAGE_PROPERTY_QUERY), allocation, buffer, allocation);
    CloseHandle(device);
    free(buffer);
}

int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if (!probe_start()) return 1;
    printf("query_size=%llu device_descriptor_size=%llu seek_descriptor_size=%llu\n",
           (unsigned long long)sizeof(STORAGE_PROPERTY_QUERY),
           (unsigned long long)sizeof(STORAGE_DEVICE_DESCRIPTOR),
           (unsigned long long)sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR));
    device_properties(L"\\\\.\\C:", "C:");
    device_properties(L"\\\\.\\PhysicalDrive0", "PhysicalDrive0");
    return probe_done(0);
}
