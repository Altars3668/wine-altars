/*
 * duptest - print a numbered test document for checking two-sided output.
 *
 *   duptest printer duplex pages
 *
 * Page N carries N black squares along its top edge, starting at the left, so
 * both the page number and which way up it was printed can be read back from
 * the raster (scripts/duplex-capture.sh does).  duplex is the DEVMODE value:
 * 1 one-sided, 2 DMDUP_VERTICAL (long edge), 3 DMDUP_HORIZONTAL (short edge).
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -municode \
 *       -o duptest.exe duptest.c -lwinspool -lgdi32
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int wmain(int argc, WCHAR **argv)
{
    HANDLE h;
    DEVMODEW *dm;
    DOCINFOW di = { sizeof(di), L"duplex order test" };
    int duplex = _wtoi(argv[2]), pages = _wtoi(argv[3]), p, i, w, sq;
    LONG size;
    HDC dc;

    if (argc < 4 || !OpenPrinterW(argv[1], &h, NULL)) { printf("cannot open printer\n"); return 1; }
    size = DocumentPropertiesW(NULL, h, argv[1], NULL, NULL, 0);
    dm = calloc(1, size);
    DocumentPropertiesW(NULL, h, argv[1], dm, NULL, DM_OUT_BUFFER);
    dm->dmFields |= DM_DUPLEX | DM_PAPERSIZE | DM_ORIENTATION;
    dm->dmDuplex = duplex;
    dm->dmPaperSize = DMPAPER_A4;
    dm->dmOrientation = DMORIENT_PORTRAIT;
    DocumentPropertiesW(NULL, h, argv[1], dm, dm, DM_IN_BUFFER | DM_OUT_BUFFER);
    printf("dmDuplex after merge: %d (fields %#lx)\n", dm->dmDuplex, dm->dmFields);

    dc = CreateDCW(L"WINSPOOL", argv[1], NULL, dm);
    if (!dc || StartDocW(dc, &di) <= 0) { printf("cannot start doc\n"); return 1; }
    w = GetDeviceCaps(dc, HORZRES);
    sq = w / 12;
    for (p = 1; p <= pages; p++)
    {
        StartPage(dc);
        for (i = 0; i < p; i++)
        {
            RECT r = { sq / 2 + i * sq * 3 / 2, sq / 2, sq / 2 + i * sq * 3 / 2 + sq, sq / 2 + sq };
            FillRect(dc, &r, GetStockObject(BLACK_BRUSH));
        }
        EndPage(dc);
    }
    EndDoc(dc);
    DeleteDC(dc);
    ClosePrinter(h);
    printf("printed %d pages\n", pages);
    return 0;
}
