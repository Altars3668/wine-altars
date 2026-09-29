# gdb: print "MAP <linux tid> <windows tid>" for every thread of a Wine process.
#
# A Wine thread's %gs points at its TEB, and TEB->ClientId.UniqueThread is at 0x48.  Threads that a
# unix library starts on its own (Mesa's gl0/gdrv0 workers) inherit %gs from the thread that created
# them, so they print the Windows thread they belong to.
import gdb

rows = []
for thread in gdb.selected_inferior().threads():
    thread.switch()
    try:
        teb = int(gdb.parse_and_eval('$gs_base'))
        tid = int(gdb.parse_and_eval('*(unsigned long *)(%d + 0x48)' % teb)) if teb else -1
    except gdb.error:
        tid = -2
    rows.append((thread.ptid[1], tid))
for lwp, tid in rows:
    print('MAP', lwp, hex(tid))
