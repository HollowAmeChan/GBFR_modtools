Option Explicit

Dim fso, shell, libDir, rootDir, scriptPath, command, argumentIndex, argument
Set fso = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")

libDir = fso.GetParentFolderName(WScript.ScriptFullName)
rootDir = fso.GetParentFolderName(libDir)
scriptPath = fso.BuildPath(rootDir, "GBFR_WorkspaceBuilder.ps1")

command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File " & QuoteArg(scriptPath)
For argumentIndex = 0 To WScript.Arguments.Count - 1
    argument = WScript.Arguments(argumentIndex)
    If Len(argument) > 0 Then command = command & " " & QuoteArg(argument)
Next

shell.Run command, 0, False

Function QuoteArg(ByVal value)
    QuoteArg = Chr(34) & value & Chr(34)
End Function
