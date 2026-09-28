@echo off
rem compare.cmd: subsets of the same fonts made by Windows's fontsub.dll and by Wine's (wfontsub.dll next to it),
rem compared table by table; README.md says what has to be in the temporary directory.  Prints one line a case and,
rem where tables differ, which; writes detail-<case>.txt and names-<case>.txt about the differences.
cd /d %TEMP%
set F=C:\Windows\Fonts
call :pair ss-hello %F%\simsun.ttc 0 Hello
call :pair ss-mix %F%\simsun.ttc 0 @mix.u16
call :pair ss1-mix %F%\simsun.ttc 1 @mix.u16
call :pair ss-s1 %F%\simsun.ttc 0 "21991 21992 21993" glyphs
call :pair ss-s2 %F%\simsun.ttc 0 "21992" glyphs
call :pair ss-s3 %F%\simsun.ttc 0 "21991 21992" glyphs
call :pair ss-s4 %F%\simsun.ttc 0 "1100 1200" glyphs
call :pair ss-s5 %F%\simsun.ttc 0 "3" glyphs
call :pair ss-s6 %F%\simsun.ttc 0 "4" glyphs
call :pair ss-g2000 %F%\simsun.ttc 0 @g2000.u16 glyphs
call :pair ss-300 %F%\simsun.ttc 0 @cjk300.u16
call :pair ss-1500 %F%\simsun.ttc 0 @cjk1500.u16
call :pair ss-2000 %F%\simsun.ttc 0 @cjk2000.u16
call :pair ss-4000 %F%\simsun.ttc 0 @cjk4000.u16
call :pair mg-hello %F%\msgothic.ttc 0 Hello
call :pair mg-mix %F%\msgothic.ttc 0 @mix.u16
call :pair mg-g1 %F%\msgothic.ttc 0 "645 646 647" glyphs
call :pair mg-g2 %F%\msgothic.ttc 0 "16076 16077" glyphs
call :pair mg-g3 %F%\msgothic.ttc 0 "2110 2111 2112" glyphs
call :pair mg-g5 %F%\msgothic.ttc 0 "646 648 700" glyphs
call :pair mg-4 %F%\msgothic.ttc 0 "4" glyphs
call :pair mg2-mix %F%\msgothic.ttc 2 @mix.u16
call :pair ar-mix %F%\arial.ttf 0 @mix.u16
call :pair tm-mix %F%\times.ttf 0 @mix.u16
call :pair dx-mix %F%\Deng.ttf 0 @mix.u16
call :pair yh-mix %F%\msyh.ttc 0 @mix.u16
call :pair sb-mix %F%\simsunb.ttf 0 @mix.u16
call :pair seg-mix %F%\segoeui.ttf 0 @mix.u16
call :pair emoji %F%\seguiemj.ttf 0 @mix.u16
call :pair tahoma %F%\tahoma.ttf 0 @mix.u16
call :pair cambria %F%\cambria.ttc 0 @mix.u16
call :pair emoji-big %F%\seguiemj.ttf 0 @emoji.u16
call :pair amiri Amiri-Regular.ttf 0 @mix.u16
call :pair andika Andika-Regular.ttf 0 @mix.u16
call :pair dejavu DejaVuSans.ttf 0 @mix.u16
call :pair gentium GentiumPlusCompact-R.ttf 0 @mix.u16
call :pair go Go-Regular.ttf 0 @mix.u16
call :pair lato Lato-Regular.ttf 0 @mix.u16
call :pair liberation LiberationSans-Regular.ttf 0 @mix.u16
call :pair ubuntu Ubuntu-M.ttf 0 @mix.u16
call :pair ubuntumono UbuntuMono-R.ttf 0 @mix.u16
call :pair fontawesome fontawesome-webfont.ttf 0 @mix.u16
call :pair ipag ipag.ttf 0 @mix.u16
call :pair uming uming.ttc 0 @mix.u16
call :pair verdana %F%\verdana.ttf 0 @mix.u16
call :pair consola %F%\consola.ttf 0 @mix.u16
call :pair calibri %F%\calibri.ttf 0 @mix.u16
call :pair georgia %F%\georgia.ttf 0 @mix.u16
del /q out-*.ttf 2>nul
goto :eof
:pair
set FONTSUB_DLL=
subsetfile.exe %2 out-%1-w.ttf %3 %4 %5 > res-w.txt
set FONTSUB_DLL=wfontsub.dll
subsetfile.exe %2 out-%1-x.ttf %3 %4 %5 > res-x.txt
set FONTSUB_DLL=
set /p RW=<res-w.txt
set /p RX=<res-x.txt
fontcmp.exe out-%1-w.ttf out-%1-x.ttf > cmp.txt
findstr /c:"OS/2:" /c:"cmap:" /c:"hmtx:" /c:"glyf:" cmp.txt >nul && fontdiff.exe %2 %3 out-%1-w.ttf out-%1-x.ttf > detail-%1.txt
findstr /c:"name:" cmp.txt >nul && (echo ---- %1 original & namedump.exe %2 %3 & echo ---- %1 windows & namedump.exe out-%1-w.ttf 0 & echo ---- %1 wine & namedump.exe out-%1-x.ttf 0) > names-%1.txt
echo %1: win %RW% / wine %RX% & type cmp.txt
goto :eof
