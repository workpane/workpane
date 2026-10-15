$from = $sshArguments[0]
$to = $sshArguments[1]
$attributes = [IO.File]::GetAttributes($from)

if (($attributes -band [IO.FileAttributes]::Directory) -ne 0) {
    [IO.Directory]::Move($from, $to)
} else {
    [IO.File]::Move($from, $to)
}
