/*
 * printprobe -- what an application actually gets from the print stack.
 *
 * "Printer properties" in any Office print dialog is one API call:
 * DocumentPropertiesW with DM_IN_PROMPT.  When that comes back with a message
 * instead of a property sheet, the question is whose message it is -- the
 * spooler's, the driver's, or the application's -- and printing from the
 * application cannot tell them apart.  This calls each step on its own and
 * prints what came back.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o printprobe.exe printprobe.c -lwinspool -lgdi32 -lcomdlg32
 *
 *   printprobe            enumerate printers and report each one's DevMode
 *   printprobe -prompt    also open the properties sheet for the default one
 *   printprobe -dlg       open the common Print dialog instead
 *   printprobe -print     render one page through the driver into a file, so
 *                         the whole path can be checked without using paper
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <winspool.h>
#include <commdlg.h>
#include <stdio.h>

static const char *u8(const WCHAR *w)
{
    static char buf[4][512];
    static int n;
    char *p = buf[n = (n + 1) & 3];
    if (!w) return "(null)";
    WideCharToMultiByte(CP_UTF8, 0, w, -1, p, sizeof(buf[0]), NULL, NULL);
    return p;
}

static void report_devmode(const DEVMODEW *dm)
{
    if (!dm) { printf("      DevMode: 没有\n"); return; }
    printf("      DevMode: 大小 %u+%u, 字段 %#lx\n",
           dm->dmSize, dm->dmDriverExtra, (unsigned long)dm->dmFields);
    printf("      纸张=%u 方向=%d 份数=%d 分辨率=%dx%d 颜色=%d 双面=%d\n",
           dm->dmPaperSize, dm->dmOrientation, dm->dmCopies,
           dm->dmPrintQuality, dm->dmYResolution, dm->dmColor, dm->dmDuplex);
}


/* What the property sheet can offer is whatever DeviceCapabilities reports, so
 * this is the list a user will actually see in the drop-downs -- and the place
 * to look when something the printer can do is not on offer. */
static void dump_caps(const WCHAR *name)
{
    static const struct { WORD cap; const char *label; } counts[] = {
        { DC_PAPERS, "纸张" }, { DC_BINS, "纸盒" }, { DC_PAPERNAMES, "纸张名" },
        { DC_BINNAMES, "纸盒名" }, { DC_MEDIATYPES, "介质类型" },
        { DC_ENUMRESOLUTIONS, "分辨率" },
    };
    size_t i;

    printf("  能力:\n");
    for (i = 0; i < ARRAYSIZE(counts); i++)
    {
        int n = DeviceCapabilitiesW(name, NULL, counts[i].cap, NULL, NULL);
        printf("    %-10s %s\n", counts[i].label, n < 0 ? "不支持" : "");
        if (n > 0) printf("      共 %d 项\n", n);
    }
    printf("    双面      %d\n", DeviceCapabilitiesW(name, NULL, DC_DUPLEX, NULL, NULL));
    printf("    逐份      %d\n", DeviceCapabilitiesW(name, NULL, DC_COLLATE, NULL, NULL));
    printf("    彩色      %d\n", DeviceCapabilitiesW(name, NULL, DC_COLORDEVICE, NULL, NULL));
    printf("    最大份数  %d\n", DeviceCapabilitiesW(name, NULL, DC_COPIES, NULL, NULL));

    /* The names are what the drop-down shows; print a few so a missing or
     * untranslated entry is visible rather than inferred. */
    {
        int n = DeviceCapabilitiesW(name, NULL, DC_PAPERNAMES, NULL, NULL);
        if (n > 0)
        {
            WCHAR *buf = calloc(n, 64 * sizeof(WCHAR));
            if (buf && DeviceCapabilitiesW(name, NULL, DC_PAPERNAMES, buf, NULL) == n)
            {
                int k;
                printf("    纸张名(前 8):");
                for (k = 0; k < n && k < 8; k++) printf(" %s", u8(buf + k * 64));
                printf("%s\n", n > 8 ? " ..." : "");
            }
            free(buf);
        }
    }
}

/* Print one page to a file.  StartDoc's lpszOutput sends the driver's output
 * to a file instead of the spooler, which exercises everything up to the point
 * where the job would be handed to CUPS -- driver, DevMode, fonts and page
 * setup -- and leaves something on disk to look at. */
/* Settings the property sheet exposes.  They are only worth exposing if they
 * reach the printer, so -duplex/-copies/-collate/-paper put them into the
 * DevMode the DC is created with, and the job ticket in the output says whether
 * they arrived. */
static int want_duplex, want_copies, want_collate = -1, want_paper, want_pages = 1;

static void print_to_file(const WCHAR *name, const WCHAR *path)
{
    /* An lpszOutput of "-" means print for real, through the spooler and the
     * print processor -- which is the only path that does manual duplex.
     * Anything else is written straight out by the driver and never reaches
     * it. */
    DOCINFOW di = { .cbSize = sizeof(di), .lpszDocName = L"printprobe",
                    .lpszOutput = wcscmp(path, L"-") ? path : NULL };
    DEVMODEW *dm = NULL;
    HANDLE pr = NULL;
    HDC dc;
    int job;

    if (want_duplex || want_copies || want_collate >= 0 || want_paper)
    {
        LONG need = DocumentPropertiesW(NULL, NULL, (WCHAR *)name, NULL, NULL, 0);
        if (need > 0 && OpenPrinterW((WCHAR *)name, &pr, NULL))
        {
            dm = calloc(1, need);
            if (dm && DocumentPropertiesW(NULL, pr, (WCHAR *)name, dm, NULL, DM_OUT_BUFFER) == IDOK)
            {
                if (want_duplex)  { dm->dmDuplex = want_duplex;  dm->dmFields |= DM_DUPLEX; }
                if (want_copies)  { dm->dmCopies = want_copies;  dm->dmFields |= DM_COPIES; }
                if (want_collate >= 0) { dm->dmCollate = want_collate; dm->dmFields |= DM_COLLATE; }
                if (want_paper)   { dm->dmPaperSize = want_paper; dm->dmFields |= DM_PAPERSIZE; }
                /* Let the driver normalise what we just set. */
                DocumentPropertiesW(NULL, pr, (WCHAR *)name, dm, dm, DM_IN_BUFFER | DM_OUT_BUFFER);
                printf("  请求: 双面=%d 份数=%d 逐份=%d 纸张=%u\n",
                       dm->dmDuplex, dm->dmCopies, dm->dmCollate, dm->dmPaperSize);
            }
            else { free(dm); dm = NULL; }
        }
    }

    dc = CreateDCW(NULL, name, NULL, dm);

    printf("\n=== 打印到文件: %s ===\n", u8(path));
    if (!dc) { printf("  CreateDC 失败\n"); return; }

    job = StartDocW(dc, &di);
    printf("  StartDoc  = %d%s\n", job, job <= 0 ? " (失败)" : "");
    if (job > 0)
    {
        /* A big page number on each sheet, and a mark near one corner: between
         * them the order the pages came out in and which way up each one is
         * are both readable off the paper, which is the only place manual
         * duplex can be checked. */
        HFONT big = CreateFontW(400, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                                0, 0, 0, 0, L"Arial");
        HFONT font = CreateFontW(72, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 0, 0, 0, 0, L"SimSun");
        int page;

        for (page = 1; page <= want_pages; page++)
        {
            WCHAR num[16], msg[128];
            RECT r = { 200, 1200, GetDeviceCaps(dc, HORZRES) - 200, 1700 };
            HGDIOBJ old;

            printf("  StartPage %d = %d\n", page, StartPage(dc));

            old = SelectObject(dc, big);
            swprintf(num, ARRAYSIZE(num), L"%d", page);
            TextOutW(dc, 300, 300, num, (int)wcslen(num));
            SelectObject(dc, font);
            swprintf(msg, ARRAYSIZE(msg),
                     L"printprobe 第 %d 页 / page %d  —— 此角为页首左侧", page, page);
            DrawTextW(dc, msg, -1, &r, DT_LEFT | DT_WORDBREAK);
            /* top-left corner mark: tells a turned sheet from an unturned one */
            Rectangle(dc, 60, 60, 260, 160);
            SelectObject(dc, old);

            printf("  EndPage   %d = %d\n", page, EndPage(dc));
        }
        DeleteObject(font);
        DeleteObject(big);
        printf("  EndDoc    = %d\n", EndDoc(dc));
    }
    DeleteDC(dc);
}

/* Store settings as the printer's own default, the way the Printers folder
 * does.  Whether they are still there when the printer is next opened is the
 * whole question: the driver used to re-derive its DevMode from the PPD on
 * every open and save the result over this. */
static void set_printer_default(const WCHAR *name)
{
    PRINTER_DEFAULTSW def = { NULL, NULL, PRINTER_ALL_ACCESS };
    PRINTER_INFO_9W info9 = { 0 };
    DEVMODEW *dm = NULL;
    HANDLE pr = NULL;
    LONG need;

    if (!OpenPrinterW((WCHAR *)name, &pr, &def))
    {
        printf("\n设为默认: OpenPrinter 失败 %lu\n", GetLastError());
        return;
    }
    need = DocumentPropertiesW(NULL, pr, (WCHAR *)name, NULL, NULL, 0);
    if (need > 0) dm = calloc(1, need);
    if (!dm || DocumentPropertiesW(NULL, pr, (WCHAR *)name, dm, NULL, DM_OUT_BUFFER) != IDOK)
    {
        printf("\n设为默认: 取不到 DevMode\n");
        free(dm); ClosePrinter(pr); return;
    }
    if (want_duplex) { dm->dmDuplex = want_duplex; dm->dmFields |= DM_DUPLEX; }
    if (want_paper)  { dm->dmPaperSize = want_paper; dm->dmFields |= DM_PAPERSIZE; }
    info9.pDevMode = dm;
    printf("\n设为默认 %s: 双面=%d 纸张=%u -> SetPrinter = %s\n", u8(name),
           dm->dmDuplex, dm->dmPaperSize,
           SetPrinterW(pr, 9, (BYTE *)&info9, 0) ? "成功" : "失败");
    free(dm);
    ClosePrinter(pr);
}

static void probe_printer(const WCHAR *name, BOOL prompt)
{
    HANDLE hp = NULL;
    LONG need;
    DEVMODEW *dm = NULL;

    printf("\n=== %s ===\n", u8(name));

    if (!OpenPrinterW((WCHAR *)name, &hp, NULL))
    {
        printf("    OpenPrinter 失败 %lu\n", GetLastError());
        return;
    }

    /* Step 1: how big is this driver's DevMode?  A driver that cannot answer
     * this is one the application will refuse to print through. */
    need = DocumentPropertiesW(NULL, hp, (WCHAR *)name, NULL, NULL, 0);
    printf("    DocumentProperties(size)   = %ld\n", need);
    if (need > 0)
    {
        dm = calloc(1, need);
        LONG r = DocumentPropertiesW(NULL, hp, (WCHAR *)name, dm, NULL, DM_OUT_BUFFER);
        printf("    DocumentProperties(out)    = %ld\n", r);
        if (r == IDOK) report_devmode(dm);
    }

    /* Step 2: the driver's own property sheet -- the "printer properties"
     * button.  DM_IN_PROMPT is the whole of it. */
    if (prompt)
    {
        LONG r = DocumentPropertiesW(GetDesktopWindow(), hp, (WCHAR *)name, dm, dm,
                                     DM_IN_BUFFER | DM_IN_PROMPT | DM_OUT_BUFFER);
        printf("    DocumentProperties(prompt) = %ld %s\n", r,
               r == IDOK ? "(用户按了确定)" : r == IDCANCEL ? "(用户取消)" : "(失败)");
        if (r < 0) printf("      GetLastError = %lu\n", GetLastError());
    }

    /* Step 3: a device context.  Without one nothing prints at all. */
    {
        HDC dc;

        dump_caps(name);
        dc = CreateDCW(NULL, name, NULL, dm);
        printf("    CreateDC                   = %s\n", dc ? "成功" : "失败");
        if (dc)
        {
            printf("      可打印区 %dx%d 像素, %dx%d 毫米, %d dpi\n",
                   GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES),
                   GetDeviceCaps(dc, HORZSIZE), GetDeviceCaps(dc, VERTSIZE),
                   GetDeviceCaps(dc, LOGPIXELSX));
            DeleteDC(dc);
        }
    }
    free(dm);
    ClosePrinter(hp);
}

int wmain(int argc, WCHAR **argv)
{
    BOOL prompt = FALSE, dlg = FALSE, setdef = FALSE;
    const WCHAR *print_to = NULL, *printer = NULL;
    for (int i = 1; i < argc; i++)
    {
        if (!wcscmp(argv[i], L"-prompt")) prompt = TRUE;
        if (!wcscmp(argv[i], L"-dlg"))    dlg = TRUE;
        if (!wcscmp(argv[i], L"-print") && i + 1 < argc) print_to = argv[++i];
        if (!wcscmp(argv[i], L"-duplex") && i + 1 < argc)  want_duplex  = _wtoi(argv[++i]);
        if (!wcscmp(argv[i], L"-copies") && i + 1 < argc)  want_copies  = _wtoi(argv[++i]);
        if (!wcscmp(argv[i], L"-collate") && i + 1 < argc) want_collate = _wtoi(argv[++i]);
        if (!wcscmp(argv[i], L"-paper") && i + 1 < argc)   want_paper   = _wtoi(argv[++i]);
        if (!wcscmp(argv[i], L"-pages") && i + 1 < argc)   want_pages   = _wtoi(argv[++i]);
        if (!wcscmp(argv[i], L"-printer") && i + 1 < argc) printer = argv[++i];
        if (!wcscmp(argv[i], L"-setdefault")) setdef = TRUE;
    }

    if (setdef)
    {
        WCHAR def[256];
        DWORD len = ARRAYSIZE(def);
        if (printer) set_printer_default(printer);
        else if (GetDefaultPrinterW(def, &len)) set_printer_default(def);
    }

    DWORD needed = 0, count = 0;
    EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2,
                  NULL, 0, &needed, &count);
    printf("printprobe (%d 位)\n枚举到的打印机:\n", (int)(sizeof(void *) * 8));
    if (needed)
    {
        BYTE *buf = calloc(1, needed);
        if (EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, NULL, 2,
                          buf, needed, &needed, &count))
        {
            PRINTER_INFO_2W *pi = (PRINTER_INFO_2W *)buf;
            for (DWORD i = 0; i < count; i++)
                printf("  %-34s 驱动=%-14s 端口=%s\n",
                       u8(pi[i].pPrinterName), u8(pi[i].pDriverName), u8(pi[i].pPortName));
            for (DWORD i = 0; i < count; i++)
                probe_printer(pi[i].pPrinterName, prompt);
        }
        free(buf);
    }
    else printf("  一台都没有\n");

    if (print_to)
    {
        WCHAR def[256];
        DWORD len = ARRAYSIZE(def);
        if (printer) print_to_file(printer, print_to);
        else if (GetDefaultPrinterW(def, &len)) print_to_file(def, print_to);
        else printf("\n没有默认打印机\n");
    }

    if (dlg)
    {
        PRINTDLGW pd = { .lStructSize = sizeof(pd), .Flags = PD_RETURNDC };
        BOOL ok = PrintDlgW(&pd);
        printf("\nPrintDlg = %s, CommDlgExtendedError = %lu\n",
               ok ? "确定" : "取消/失败", CommDlgExtendedError());
        if (pd.hDC) DeleteDC(pd.hDC);
    }
    return 0;
}
