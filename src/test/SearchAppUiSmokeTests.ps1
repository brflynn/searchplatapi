param(
    [Parameter(Mandatory = $false)]
    [string]$AppPath
)

$ErrorActionPreference = "Stop"
if (-not $AppPath)
{
    $AppPath = Join-Path $PSScriptRoot "..\app\x64\Debug\app\SearchApp.exe"
}

Add-Type -AssemblyName UIAutomationClient
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class SearchAppUiNative
{
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool RegisterHotKey(IntPtr window, int id, uint modifiers, uint key);

    [DllImport("user32.dll")]
    public static extern bool UnregisterHotKey(IntPtr window, int id);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowEx(
        IntPtr parent, IntPtr childAfter, string className, string windowName);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool ShowWindow(IntPtr window, int command);

    [DllImport("user32.dll")]
    public static extern bool SetWindowPos(
        IntPtr window, IntPtr insertAfter, int x, int y, int width, int height, uint flags);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr SendMessageTimeout(
        IntPtr window, uint message, UIntPtr wParam, IntPtr lParam,
        uint flags, uint timeout, out UIntPtr result);
}
'@

function Assert-True([bool]$Condition, [string]$Message)
{
    if (-not $Condition)
    {
        throw $Message
    }
}

function Find-AutomationId($Window, [string]$AutomationId)
{
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::AutomationIdProperty,
        $AutomationId)
    return $Window.FindFirst(
        [System.Windows.Automation.TreeScope]::Descendants,
        $condition)
}

function Find-Name($Window, [string]$Name)
{
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::NameProperty,
        $Name)
    return $Window.FindFirst(
        [System.Windows.Automation.TreeScope]::Descendants,
        $condition)
}

$ctrlShiftF = 0x0002 -bor 0x0004 -bor 0x4000
$fKey = 0x46
$probeHotkeyId = 99
$hotkeyAvailable = [SearchAppUiNative]::RegisterHotKey(
    [IntPtr]::Zero, $probeHotkeyId, $ctrlShiftF, $fKey)
if ($hotkeyAvailable)
{
    [SearchAppUiNative]::UnregisterHotKey([IntPtr]::Zero, $probeHotkeyId) | Out-Null
}
Assert-True $hotkeyAvailable "Ctrl+Shift+F was already registered before SearchApp started."

$process = $null
try
{
    $resolvedAppPath = (Resolve-Path $AppPath).Path
    $process = Start-Process `
        -FilePath $resolvedAppPath `
        -WorkingDirectory (Split-Path $resolvedAppPath) `
        -PassThru

    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do
    {
        Start-Sleep -Milliseconds 200
        $process.Refresh()
    }
    while (-not $process.HasExited -and
           $process.MainWindowHandle -eq 0 -and
           [DateTime]::UtcNow -lt $deadline)

    Assert-True (-not $process.HasExited) "SearchApp exited during startup."
    Assert-True ($process.MainWindowHandle -ne 0) "SearchApp did not create a main window."

    $windowHandle = [IntPtr]$process.MainWindowHandle
    $window = [System.Windows.Automation.AutomationElement]::FromHandle($windowHandle)
    $searchBox = Find-AutomationId $window "SearchTextBox"
    $statusText = Find-AutomationId $window "StatusText"
    Assert-True ($null -ne $searchBox) "SearchTextBox was not found through UI Automation."
    Assert-True ($null -ne $statusText) "StatusText was not found through UI Automation."

    $hotkeyReserved = -not [SearchAppUiNative]::RegisterHotKey(
        [IntPtr]::Zero, $probeHotkeyId, $ctrlShiftF, $fKey)
    if (-not $hotkeyReserved)
    {
        [SearchAppUiNative]::UnregisterHotKey([IntPtr]::Zero, $probeHotkeyId) | Out-Null
    }
    Assert-True $hotkeyReserved "SearchApp did not reserve Ctrl+Shift+F."

    $valuePattern = $searchBox.GetCurrentPattern(
        [System.Windows.Automation.ValuePattern]::Pattern)
    $valuePattern.SetValue("windows")

    $maxLatency = 0
    $timeouts = 0
    1..30 | ForEach-Object {
        $result = [UIntPtr]::Zero
        $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        $responsive = [SearchAppUiNative]::SendMessageTimeout(
            $windowHandle, 0, [UIntPtr]::Zero, [IntPtr]::Zero,
            0x0002, 1000, [ref]$result)
        $stopwatch.Stop()
        if ($responsive -eq [IntPtr]::Zero)
        {
            $timeouts++
        }
        $maxLatency = [Math]::Max($maxLatency, $stopwatch.ElapsedMilliseconds)
        Start-Sleep -Milliseconds 100
    }
    Assert-True ($timeouts -eq 0) "SearchApp's UI thread stopped responding during search."

    $valuePattern = $searchBox.GetCurrentPattern(
        [System.Windows.Automation.ValuePattern]::Pattern)
    Assert-True ($valuePattern.Current.Value -eq "windows") "SearchTextBox did not retain typed input."
    Assert-True ($statusText.Current.Name -match "results") "Typing did not produce a search result status."

    [SearchAppUiNative]::ShowWindow($windowHandle, 9) | Out-Null
    Start-Sleep -Milliseconds 300
    [SearchAppUiNative]::SetWindowPos(
        $windowHandle, [IntPtr]::Zero, 80, 80, 900, 700, 0x0040) | Out-Null
    Start-Sleep -Seconds 1
    $backgroundPanelText = Find-Name $window "BACKGROUND INDEX"
    Assert-True (
        $null -eq $backgroundPanelText -or $backgroundPanelText.Current.IsOffscreen
    ) "Side status panels should collapse in a narrow window."

    [SearchAppUiNative]::SetWindowPos(
        $windowHandle, [IntPtr]::Zero, 40, 40, 1500, 900, 0x0040) | Out-Null
    Start-Sleep -Seconds 1
    $backgroundPanelText = Find-Name $window "BACKGROUND INDEX"
    Assert-True (
        $null -ne $backgroundPanelText -and -not $backgroundPanelText.Current.IsOffscreen
    ) "Side status panels should be visible in a wide window."

    $messageOnly = [IntPtr](-3)
    $hotkeyWindow = [SearchAppUiNative]::FindWindowEx(
        $messageOnly, [IntPtr]::Zero, "SearchAppHotkeyMessageWindow", $null)
    Assert-True ($hotkeyWindow -ne [IntPtr]::Zero) "SearchApp hotkey message window was not created."

    [SearchAppUiNative]::PostMessage(
        $hotkeyWindow, 0x0312, [UIntPtr]::new(1), [IntPtr]::Zero) | Out-Null
    Start-Sleep -Seconds 1
    $valuePattern = $searchBox.GetCurrentPattern(
        [System.Windows.Automation.ValuePattern]::Pattern)
    Assert-True ($valuePattern.Current.Value -eq "") "Ctrl+Shift+F did not clear and focus search."

    Write-Host "PASS: Search input, responsiveness, resize behavior, and Ctrl+Shift+F."
    Write-Host "Maximum measured UI message latency: $maxLatency ms"
}
finally
{
    if ($process -and -not $process.HasExited)
    {
        Stop-Process -Id $process.Id
        Wait-Process -Id $process.Id -ErrorAction SilentlyContinue
    }
}
