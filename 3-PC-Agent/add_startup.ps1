# Adds the SmartLab agent to Windows startup (runs hidden/minimised after login).
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $dir "SmartLabAgent.exe"
$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath("Startup")) "SmartLab Agent.lnk"))
if (Test-Path $exe) {
    $lnk.TargetPath = $exe
    $lnk.Arguments = ""
} else {
    $py = (Get-Command python -ErrorAction Stop).Source
    $lnk.TargetPath = Join-Path (Split-Path -Parent $py) "pythonw.exe"
    $lnk.Arguments = '"' + (Join-Path $dir "pc_agent.py") + '"'
}
$lnk.WorkingDirectory = $dir
$lnk.WindowStyle = 7
$lnk.Save()
Write-Host "Added to Windows startup:" $lnk.TargetPath $lnk.Arguments
