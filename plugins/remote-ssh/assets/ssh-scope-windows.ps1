$scopeNonce = $sshArguments[0]
$scopeLease = [int]$sshArguments[1]
$scopeDirectory = [Workpane.Ssh.Paths]::CreateScope()
$scope = $null
$scopeEvents = $null
$stages = @{}

try {
    $scope = [Workpane.Ssh.Jobs]::new($scopeNonce, $sshInput, $scopeLease, $sshInputFile.SafeFileHandle.DangerousGetHandle())
    $scopeEvents = [Workpane.Ssh.Events]::new($scopeDirectory, [Console]::Out, $scope)
    [Console]::Out.WriteLine('SCOPE ' + $scopeDirectory + ' ' + $PID)

    while ($true) {
        $command = $scope.ReadControl()

        if ($null -eq $command -or $command -eq 'quit') {
            break
        }

        if ($command -eq 'account') {
            $account = [Security.Principal.WindowsIdentity]::GetCurrent().Name
            $accountHome = [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
            $shell = [IO.Path]::Combine([Environment]::SystemDirectory, 'WindowsPowerShell/v1.0/powershell.exe')
            $fields = @($account, $accountHome, $shell) | ForEach-Object { [Convert]::ToBase64String($sshEncoding.GetBytes($_)) }
            [Console]::Out.WriteLine('ACCOUNT ' + ($fields -join ' '))
            continue
        }

        if ($command -eq 'ping') {
            $scope.Renew()
            [Console]::Out.WriteLine('PONG')
            continue
        }

        $fields = $command.Split(' ')

        if ($fields.Length -lt 2 -or $fields[1] -notmatch '^[1-9][0-9]{0,9}$') {
            throw 'The SSH control command is invalid'
        }

        $identity = [int]::Parse($fields[1], [Globalization.CultureInfo]::InvariantCulture)
        $directory = [IO.Path]::Combine($scopeDirectory, 'program.' + $identity)
        $nativeIdentity = $identity.ToString('x32', [Globalization.CultureInfo]::InvariantCulture)

        if ($fields[0] -eq 'stage' -and $fields.Length -eq 3) {
            if ($stages.Count -ge 32 -or $stages.ContainsKey($identity)) {
                throw 'The SSH staging admission is invalid'
            }

            $path = $sshEncoding.GetString([Convert]::FromBase64String($fields[2]))
            try {
                [Workpane.Ssh.Paths]::CreatePrivate($path)
            }

            catch {
                [Console]::Out.WriteLine('ERROR ' + $identity + ' remote_file_write_failed')
                continue
            }

            $stages.Add($identity, $path)
            [Console]::Out.WriteLine('STAGED ' + $identity)
            continue
        }

        if ($fields[0] -eq 'unstage' -and $fields.Length -eq 2) {
            if (-not $stages.ContainsKey($identity)) {
                throw 'The SSH staging identity is invalid'
            }

            [IO.Directory]::Delete($stages[$identity], $true)
            $stages.Remove($identity)
            [Console]::Out.WriteLine('UNSTAGED ' + $identity)
            continue
        }

        if ($fields[0] -eq 'open' -and $fields.Length -eq 2) {
            $job = $scope.Open($nativeIdentity)
            [Workpane.Ssh.Paths]::CreatePrivate($directory)
            [IO.File]::WriteAllText([IO.Path]::Combine($directory, 'job'), $job, [Text.Encoding]::ASCII)
            $scopeEvents.Open($identity)
            [Console]::Out.WriteLine('OPEN ' + $identity)
            continue
        }

        if ($fields[0] -eq 'stop' -and $fields.Length -eq 2) {
            $scopeEvents.Stop($identity)
            $scope.Stop($nativeIdentity)

            if ([IO.Directory]::Exists($directory)) {
                [IO.Directory]::Delete($directory, $true)
            }

            [Console]::Out.WriteLine('STOPPED ' + $identity)
            continue
        }

        if ($fields[0] -eq 'find' -and $fields.Length -ge 3 -and $fields.Length -le 259) {
            $program = $sshEncoding.GetString([Convert]::FromBase64String($fields[2]))
            $directories = [Collections.Generic.List[string]]::new()

            for ($index = 3; $index -lt $fields.Length; $index++) {
                $directories.Add($sshEncoding.GetString([Convert]::FromBase64String($fields[$index])))
            }

            $found = [Workpane.Ssh.Paths]::Find($program, $directories.ToArray())

            if ($null -eq $found) {
                [Console]::Out.WriteLine('ABSENT ' + $identity)
            } else {
                [Console]::Out.WriteLine('FOUND ' + $identity + ' ' + [Convert]::ToBase64String($sshEncoding.GetBytes($found)))
            }

            continue
        }

        if ($fields[0] -eq 'info' -and $fields.Length -eq 3) {
            $path = $sshEncoding.GetString([Convert]::FromBase64String($fields[2]))

            try {
                $metadata = [Workpane.Ssh.Paths]::Metadata($path)
                [Console]::Out.WriteLine('INFO ' + $identity + ' ' + $metadata)
            }

            catch {
                $reason = 'remote_path_inspection_failed'
                $problem = $_.Exception.InnerException

                if ($problem -is [IO.FileNotFoundException] -or $problem -is [IO.DirectoryNotFoundException]) {
                    $reason = 'remote_path_unavailable'
                }

                [Console]::Out.WriteLine('ERROR ' + $identity + ' ' + $reason)
            }

            continue
        }

        if ($fields[0] -eq 'resolve' -and $fields.Length -eq 3) {
            $parameters = $sshEncoding.GetString([Convert]::FromBase64String($fields[2])) | ConvertFrom-Json

            try {
                $resolved = [Workpane.Ssh.Paths]::Resolve($parameters.root, $parameters.path, $parameters.missing)
                [Console]::Out.WriteLine('RESOLVED ' + $identity + ' ' + [Convert]::ToBase64String($sshEncoding.GetBytes($resolved)))
            }

            catch {
                $reason = 'remote_path_inspection_failed'
                $problem = $_.Exception.InnerException

                if ($problem -is [UnauthorizedAccessException]) {
                    $reason = 'remote_path_outside_workspace'
                }

                if ($problem -is [ComponentModel.Win32Exception] -and ($problem.NativeErrorCode -eq 2 -or $problem.NativeErrorCode -eq 3)) {
                    $reason = 'remote_path_unavailable'
                }

                [Console]::Out.WriteLine('ERROR ' + $identity + ' ' + $reason)
            }

            continue
        }

        throw 'The SSH control command is invalid'
    }
}

finally {
    if ($null -ne $scopeEvents) {
        $scopeEvents.Dispose()
    }

    if ($null -ne $scope) {
        $scope.Dispose()
    }

    foreach ($path in $stages.Values) {
        [IO.Directory]::Delete($path, $true)
    }

    [IO.Directory]::Delete($scopeDirectory, $true)
}
