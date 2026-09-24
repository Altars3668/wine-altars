/*
 * msoprintstate - read what MSO.DLL decided about the printer's Print Schema.
 *
 * Word offers "Print on both sides" only while MSO keeps the printer's
 * Print Schema provider usable: it reads the duplex options out of
 * PTGetPrintCapabilities, then round-trips Word's DEVMODE through a
 * PrintTicket, and any failure along the way marks the provider broken and
 * leaves Word with manual duplex alone -- with nothing to say so.  This finds
 * that object in a running WINWORD.EXE, read-only, by the duplex feature name
 * it points at, and prints its fields.
 *
 *   msoprintstate <MSO.DLL load address in hex>
 *
 * The address is where MSO.DLL is mapped in Word (the first line of it in
 * /proc/<pid>/maps).  Offsets are those of MSO 16.0.20326.20158:
 *   +0x08 provider handle        +0x10 provider usable (valid)
 *   +0x12 failed                 +0x13 capabilities parsed
 *   +0x18 staple feature name    +0x28 duplex feature name
 *   +0x30 duplex options offered (1 short edge, 2 long edge)
 *   +0x34 duplex chosen (0 one-sided, 1 short edge, 2 long edge)
 *   +0x38 printer name
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: msoprintstate <MSO.DLL base in hex>\n"); return 1; }
    ULONG_PTR mso = strtoull(argv[1], NULL, 16);
    ULONG_PTR dupA = mso + 0x1a4f260, dupB = mso + 0x1a4f2a8, stA = mso + 0x1a4f228, stB = mso + 0x1ba48b8;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0), proc;
    PROCESSENTRY32W pe;
    DWORD pid = 0; MEMORY_BASIC_INFORMATION mbi; BYTE *addr = NULL, *buf; SIZE_T got; int hits = 0;

    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (!_wcsicmp(pe.szExeFile, L"WINWORD.EXE")) pid = pe.th32ProcessID;
    proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    printf("WINWORD pid=%#lx, 找 +0x28 ∈ {%p,%p}\n", pid, (void *)dupA, (void *)dupB);
    while (VirtualQueryEx(proc, addr, &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && mbi.RegionSize < ((SIZE_T)1 << 31))
        {
            buf = malloc(mbi.RegionSize);
            if (buf && ReadProcessMemory(proc, mbi.BaseAddress, buf, mbi.RegionSize, &got))
                for (SIZE_T i = 0x28; i + 0x240 < got; i += 8)
                {
                    ULONG_PTR v = *(ULONG_PTR *)(buf + i);
                    BYTE *o;
                    if (v != dupA && v != dupB) continue;
                    o = buf + i - 0x28;
                    hits++;
                    printf("this=%p  duplex-name=%s  staple-name=%s\n", (void *)((ULONG_PTR)mbi.BaseAddress + i - 0x28),
                           v == dupA ? "JobDuplexAllDocumentsContiguously" : "DocumentDuplex",
                           *(ULONG_PTR *)(o + 0x18) == stA ? "JobStapleAllDocuments" : *(ULONG_PTR *)(o + 0x18) == stB ? "DocumentStaple" : "?");
                    printf("   +08 provider=%016llx\n", *(unsigned long long *)(o + 8));
                    printf("   +10 valid=%02x +11=%02x +12 err=%02x +13 parsed=%02x\n", o[0x10], o[0x11], o[0x12], o[0x13]);
                    printf("   +20 staple=%08lx +24=%08lx  +30 duplex-mask=%08lx  +34 cur=%08lx\n", *(DWORD *)(o + 0x20), *(DWORD *)(o + 0x24), *(DWORD *)(o + 0x30), *(DWORD *)(o + 0x34));
                    printf("   +38 printer=[%.40ls]\n", (WCHAR *)(o + 0x38));
                    printf("   +240=[%.60ls]\n", (WCHAR *)(o + 0x240));
                }
            free(buf);
        }
        addr = (BYTE *)mbi.BaseAddress + mbi.RegionSize;
    }
    printf("共 %d 处\n", hits);
    return 0;
}
