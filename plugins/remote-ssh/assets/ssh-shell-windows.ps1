$history = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($env:WORKPANE_HISTORY_FILE))
Remove-Item Env:WORKPANE_HISTORY_FILE
Import-Module PSReadLine -ErrorAction Stop
$readerHistory = (Get-PSReadLineOption).HistorySavePath

if (-not [IO.File]::Exists($history) -and [IO.File]::Exists($readerHistory)) {
    [IO.File]::Copy($readerHistory, $history, $false)
}

Set-PSReadLineOption -HistorySavePath $history
$global:WorkpanePrompt = $function:prompt

function global:prompt {
    $location = (Get-Location).ProviderPath -replace '\\', '/'
    [Console]::Write("$([char]27)]7;file://localhost/$location$([char]7)")
    & $global:WorkpanePrompt
}
