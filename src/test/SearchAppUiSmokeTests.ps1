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
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

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

function Invoke-Control($Control)
{
    Assert-True ($null -ne $Control) "Required UI control was not found."
    $Control.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
}

function Invoke-ClientControl($Control, [IntPtr]$WindowHandle)
{
    Assert-True ($null -ne $Control) "Required client-area control was not found."
    Assert-True (-not $Control.Current.IsOffscreen -and $Control.Current.IsEnabled) "Client-area control is not visible and enabled."
    # UI Automation returns physical screen coordinates; avoid DPI virtualization.
    $previousDpiContext = [SearchAppUiNative]::SetThreadDpiAwarenessContext([IntPtr](-4))
    try
    {
        $bounds = $Control.Current.BoundingRectangle
        $x = [int]($bounds.X + $bounds.Width / 2)
        $y = [int]($bounds.Y + $bounds.Height / 2)
        $screenPosition = [IntPtr](($x -band 0xffff) -bor (($y -band 0xffff) -shl 16))
        $hit = [UIntPtr]::Zero
        Assert-True (
            [SearchAppUiNative]::SendMessageTimeout(
                $WindowHandle, 0x0084, [UIntPtr]::Zero, $screenPosition,
                0x0002, 1000, [ref]$hit) -ne [IntPtr]::Zero
        ) "Button hit testing timed out."
        Assert-True ($hit.ToUInt64() -eq 1) "Button is inside a non-client drag region rather than clickable client area."
    }
    finally
    {
        [SearchAppUiNative]::SetThreadDpiAwarenessContext($previousDpiContext) | Out-Null
    }
    Invoke-Control $Control
}

function Wait-Ui([scriptblock]$Condition, [string]$Message)
{
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do
    {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw $Message
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
$fixtureRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("SearchAppUiSmoke_" + [Guid]::NewGuid().ToString("N"))
$fixtureToken = "SearchAppSmoke" + [Guid]::NewGuid().ToString("N")
$fixtureName = $fixtureToken + ".txt"
$previousSmokeRoot = $env:SEARCHAPP_UI_SMOKE_ROOT
try
{
    New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
    Set-Content -LiteralPath (Join-Path $fixtureRoot $fixtureName) -Value "Smoke content retained during exclusions."
    $env:SEARCHAPP_UI_SMOKE_ROOT = $fixtureRoot
    $resolvedAppPath = (Resolve-Path $AppPath).Path
    $process = Start-Process `
        -FilePath $resolvedAppPath `
        -WorkingDirectory (Split-Path $resolvedAppPath) `
        -PassThru
    $env:SEARCHAPP_UI_SMOKE_ROOT = $previousSmokeRoot

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
    $startupResult = [UIntPtr]::Zero
    Assert-True (
        [SearchAppUiNative]::SendMessageTimeout(
            $windowHandle, 0, [UIntPtr]::Zero, [IntPtr]::Zero,
            0x0002, 1000, [ref]$startupResult) -ne [IntPtr]::Zero
    ) "SearchApp created a window but its UI did not respond during startup."
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
    $compactReindex = Find-AutomationId $window "CompactReindexButton"
    Assert-True ($null -ne $compactReindex -and -not $compactReindex.Current.IsOffscreen) "Reindex must remain available in narrow layout."
    Assert-True (-not (Find-AutomationId $window "CompactIndexStatusText").Current.IsOffscreen) "Reindex status must remain visible in narrow layout."
    Invoke-ClientControl (Find-AutomationId $window "IndexSettingsButton") $windowHandle
    Wait-Ui { $null -ne (Find-AutomationId $window "IndexSettingsDialog") } "Settings did not open in narrow layout."
    $dialog = Find-AutomationId $window "IndexSettingsDialog"
    Wait-Ui { $null -ne (Find-Name $dialog "No exclusions.") } "Empty exclusion state was not shown."
    Invoke-Control (Find-Name $dialog "Close")
    Wait-Ui { $null -eq (Find-AutomationId $window "IndexSettingsDialog") } "Settings did not close."
    Start-Sleep -Milliseconds 500
    Invoke-ClientControl $compactReindex $windowHandle
    Wait-Ui { $statusText.Current.Name -match "Local rescan queued" } "Compact reindex did not activate."

    [SearchAppUiNative]::SetWindowPos(
        $windowHandle, [IntPtr]::Zero, 40, 40, 1500, 900, 0x0040) | Out-Null
    Start-Sleep -Seconds 1
    $backgroundPanelText = Find-Name $window "BACKGROUND INDEX"
    Assert-True (
        $null -ne $backgroundPanelText -and -not $backgroundPanelText.Current.IsOffscreen
    ) "Side status panels should be visible in a wide window."

    Invoke-Control (Find-AutomationId $window "ReindexButton")
    $valuePattern.SetValue($fixtureToken)
    Wait-Ui { $null -ne (Find-Name $window $fixtureName) } "Fixture did not appear after local reindex."
    Invoke-Control (Find-Name $window "Result actions")
    Wait-Ui { $null -ne (Find-Name $window "Request Windows Search content indexing for folder") } "Result actions menu did not open."
    $scopeAction = Find-Name $window "Request Windows Search content indexing for folder"
    Assert-True ($null -ne $scopeAction -and -not $scopeAction.Current.IsEnabled) "Smoke mode must disable real Windows Search scope changes."
    Invoke-Control (Find-Name $window "Exclude extension: .txt")
    Wait-Ui { $statusText.Current.Name -match "^0 results" } "Extension exclusion did not immediately refresh results."
    Assert-True ($null -eq (Find-Name $window $fixtureName)) "Excluded extension remained visible."
    Invoke-ClientControl (Find-AutomationId $window "IndexSettingsButton") $windowHandle
    Wait-Ui { $null -ne (Find-Name $window "Restore .txt") } "Persisted extension exclusion was not listed."
    Invoke-Control (Find-Name $window "Restore .txt")
    Wait-Ui { $null -ne (Find-Name $window "No exclusions.") } "Extension restore did not update settings."
    Invoke-Control (Find-Name (Find-AutomationId $window "IndexSettingsDialog") "Close")
    Wait-Ui { $null -ne (Find-Name $window $fixtureName) } "Restoring extension did not bring retained data back."

    Invoke-Control (Find-Name $window "Result actions")
    Wait-Ui { $null -ne (Find-Name $window ("Exclude folder: " + $fixtureRoot.ToLowerInvariant())) } "Folder action menu did not open."
    Invoke-Control (Find-Name $window ("Exclude folder: " + $fixtureRoot.ToLowerInvariant()))
    Wait-Ui { $statusText.Current.Name -match "^0 results" } "Folder exclusion did not refresh results."
    Invoke-Control (Find-AutomationId $window "IndexSettingsButton")
    $restoreFolderName = "Restore " + $fixtureRoot.ToLowerInvariant()
    Wait-Ui { $null -ne (Find-Name $window $restoreFolderName) } "Folder exclusion was not listed."
    Invoke-Control (Find-Name $window $restoreFolderName)
    Wait-Ui { $null -ne (Find-Name $window "No exclusions.") } "Folder restore did not update settings."
    Invoke-Control (Find-Name (Find-AutomationId $window "IndexSettingsDialog") "Close")
    Wait-Ui { $null -ne (Find-Name $window $fixtureName) } "Restoring folder did not bring retained data back."

    $messageOnly = [IntPtr](-3)
    $hotkeyWindow = [SearchAppUiNative]::FindWindowEx(
        $messageOnly, [IntPtr]::Zero, "SearchAppHotkeyMessageWindow", $null)
    Assert-True ($hotkeyWindow -ne [IntPtr]::Zero) "SearchApp hotkey message window was not created."

    [SearchAppUiNative]::PostMessage(
        $hotkeyWindow, 0x0312, [UIntPtr]::new(1), [IntPtr]::Zero) | Out-Null
    Start-Sleep -Seconds 1
    $valuePattern = $searchBox.GetCurrentPattern(
        [System.Windows.Automation.ValuePattern]::Pattern)
    if ($valuePattern.Current.Value -ne "")
    {
        # A foreground window is hidden first; the next hotkey restores and clears it.
        [SearchAppUiNative]::PostMessage(
            $hotkeyWindow, 0x0312, [UIntPtr]::new(1), [IntPtr]::Zero) | Out-Null
        Start-Sleep -Seconds 1
    }
    Assert-True ($valuePattern.Current.Value -eq "") "Ctrl+Shift+F did not clear and focus search."

    Write-Host "PASS: Search, responsiveness, resize, native client-area hit testing and settings activation in narrow/wide layouts, compact reindex, folder/extension exclusion and restore, safe scope controls, Ctrl+Shift+F."
    Write-Host "Maximum measured UI message latency: $maxLatency ms"
}
catch
{
    Write-Host "FAIL: $($_.Exception.Message)"
    throw
}
finally
{
    $env:SEARCHAPP_UI_SMOKE_ROOT = $previousSmokeRoot
    if ($process -and -not $process.HasExited)
    {
        Stop-Process -Id $process.Id
        Wait-Process -Id $process.Id -ErrorAction SilentlyContinue
    }
    foreach ($file in @($fixtureName, "ui-smoke.db", "ui-smoke.db-wal", "ui-smoke.db-shm"))
    {
        $fixtureFile = Join-Path $fixtureRoot $file
        for ($attempt = 0; $attempt -lt 30 -and (Test-Path -LiteralPath $fixtureFile); $attempt++)
        {
            try { Remove-Item -LiteralPath $fixtureFile }
            catch
            {
                if ($attempt -eq 29) { throw }
                Start-Sleep -Milliseconds 100
            }
        }
    }
    if (Test-Path -LiteralPath $fixtureRoot) { Remove-Item -LiteralPath $fixtureRoot }
}
