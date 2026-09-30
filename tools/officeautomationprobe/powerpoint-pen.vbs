' PowerPoint slide show kept open while something draws on it with the pen: sets the pointer to the pen,
' writes "show" to the flag file and waits 12 s (drag the mouse over the slide meanwhile, for example with
' tools/sendkeys: move:x,y press move:... release), then draws a line with View.DrawLine, writes "drawn",
' waits 12 s and writes "end".  Leaves PowerPoint running; the caller closes it.
' Usage: cscript powerpoint-pen.vbs <flag file>
Option Explicit
Dim app, pres, slide, fso, flag, view
Set fso = CreateObject("Scripting.FileSystemObject")
flag = WScript.Arguments(0)
Sub stage(name, ms)
    Dim f
    Set f = fso.CreateTextFile(flag, True)
    f.WriteLine name
    f.Close
    WScript.Sleep ms
End Sub
Set app = CreateObject("PowerPoint.Application")
app.Visible = True
Set pres = app.Presentations.Add()
Set slide = pres.Slides.Add(1, 12)
pres.SlideShowSettings.Run
WScript.Sleep 3000
Set view = pres.SlideShowWindow.View
view.PointerType = 2
WScript.Echo "pointer type " & view.PointerType
stage "show", 12000
view.DrawLine 200, 100, 700, 400
WScript.Echo "drew a line"
stage "drawn", 12000
stage "end", 1000
