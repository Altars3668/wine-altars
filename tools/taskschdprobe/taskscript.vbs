Set service = CreateObject("Schedule.Service")
service.Connect
WScript.Echo "connected " & service.Connected & ", highest version " & Hex(service.HighestVersion)
Set def = service.NewTask(0)
def.RegistrationInfo.Author = "Wine"
Set t = def.Triggers.Create(2)
t.StartBoundary = "2020-01-02T03:04:05"
t.DaysInterval = 2
Set a = def.Actions.Create(0)
a.Path = "notepad.exe"
a.HideAppWindow = True
def.Settings.Volatile = True
For Each trig In def.Triggers
  WScript.Echo "trigger type " & trig.Type & ", every " & trig.DaysInterval & " days"
Next
For Each act In def.Actions
  WScript.Echo "action " & act.Type & ": " & act.Path
Next
WScript.Echo def.XmlText
