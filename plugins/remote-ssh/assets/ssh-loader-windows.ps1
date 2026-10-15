$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Workpane.Ssh {
    public static class Streams {
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr GetStdHandle(int identifier);

        public static FileStream Open(int identifier, FileAccess access) {
            return new FileStream(new SafeFileHandle(GetStdHandle(identifier), false), access, 4096, true);
        }

        public static string Source(Stream input, int maximum) {
            byte[] bytes = new byte[maximum];
            int count = 0;

            while (true) {
                int value = input.ReadByte();

                if (value == 10) {
                    return System.Text.Encoding.ASCII.GetString(bytes, 0, count);
                }

                if (value < 0 || value > 127 || value == 0) {
                    throw new IOException("The SSH source header is invalid");
                }

                if (count == bytes.Length) {
                    throw new IOException("The SSH source header exceeds its bound");
                }

                bytes[count++] = (byte)value;
            }
        }
    }
}
'@
$sshInputFile = [Workpane.Ssh.Streams]::Open(-10, [System.IO.FileAccess]::Read)
$sshInput = [System.IO.BufferedStream]::new($sshInputFile, 4096)
$sshOutput = [Workpane.Ssh.Streams]::Open(-11, [System.IO.FileAccess]::Write)
$sshError = [Workpane.Ssh.Streams]::Open(-12, [System.IO.FileAccess]::Write)
$sshEncoding = [System.Text.UTF8Encoding]::new($false, $true)
$sshWriter = [System.IO.StreamWriter]::new($sshOutput, $sshEncoding, 1024, $true)
$sshErrorWriter = [System.IO.StreamWriter]::new($sshError, $sshEncoding, 1024, $true)
$sshWriter.AutoFlush = $true
$sshErrorWriter.AutoFlush = $true
[Console]::SetOut($sshWriter)
[Console]::SetError($sshErrorWriter)

try {
    $sshSource = [Convert]::FromBase64String([Workpane.Ssh.Streams]::Source($sshInput, 65536))

    if ($sshSource.Length -gt 49152) {
        throw 'The SSH source exceeds its bound'
    }

    & ([ScriptBlock]::Create($sshEncoding.GetString($sshSource)))
}

catch {
    $sshProblem = $_.Exception.GetBaseException()
    $sshCode = 0

    if ($sshProblem -is [System.ComponentModel.Win32Exception]) {
        $sshCode = $sshProblem.NativeErrorCode
    }

    [Console]::Error.WriteLine('WORKPANE_SSH_FAILURE ' + $sshProblem.GetType().FullName + ' ' + $sshCode + ' ' + $_.InvocationInfo.ScriptLineNumber)
    exit 1
}

finally {
    $sshWriter.Dispose()
    $sshErrorWriter.Dispose()
    $sshInput.Dispose()
    $sshInputFile.Dispose()
    $sshOutput.Dispose()
    $sshError.Dispose()
}
