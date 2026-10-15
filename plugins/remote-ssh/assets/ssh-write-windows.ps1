Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace Workpane.Ssh {
    public static class FileWrites {
        [StructLayout(LayoutKind.Sequential)]
        private struct Mapping {
            public uint Read;
            public uint Write;
            public uint Execute;
            public uint All;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint sharing, IntPtr security, uint creation, uint flags, IntPtr template);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern uint GetNamedSecurityInfoW(string path, uint kind, uint information, IntPtr owner, IntPtr group, IntPtr access, IntPtr audit, out IntPtr descriptor);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool OpenProcessToken(IntPtr process, uint access, out SafeFileHandle token);

        [DllImport("kernel32.dll")]
        private static extern IntPtr GetCurrentProcess();

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool CreatePrivateObjectSecurityEx(IntPtr parent, IntPtr creator, out IntPtr created, IntPtr kind, bool directory, uint flags, SafeFileHandle token, ref Mapping mapping);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool DestroyPrivateObjectSecurity(ref IntPtr descriptor);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool GetSecurityDescriptorDacl(IntPtr descriptor, out bool present, out IntPtr access, out bool defaulted);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool GetSecurityDescriptorControl(IntPtr descriptor, out ushort control, out uint revision);

        [DllImport("advapi32.dll")]
        private static extern uint SetSecurityInfo(SafeFileHandle file, uint kind, uint information, IntPtr owner, IntPtr group, IntPtr access, IntPtr audit);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetFileInformationByHandle(SafeFileHandle file, int kind, ref byte information, uint size);

        [DllImport("kernel32.dll")]
        private static extern IntPtr LocalFree(IntPtr memory);

        private static void Check(bool value) {
            if (!value) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
        }

        private static IntPtr Permissions(string path, uint information) {
            IntPtr descriptor;
            uint code = GetNamedSecurityInfoW(path, 1, information, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, out descriptor);

            if (code != 0) {
                throw new Win32Exception((int)code);
            }

            return descriptor;
        }

        private static void ApplyPermissions(SafeFileHandle file, IntPtr descriptor) {
            bool present;
            bool defaulted;
            IntPtr access;
            ushort control;
            uint revision;
            Check(GetSecurityDescriptorDacl(descriptor, out present, out access, out defaulted));
            Check(GetSecurityDescriptorControl(descriptor, out control, out revision));
            uint inheritance = (control & 0x1000) != 0 ? 0x80000000U : 0x20000000U;
            uint code = SetSecurityInfo(file, 1, 4U | inheritance, IntPtr.Zero, IntPtr.Zero, present ? access : IntPtr.Zero, IntPtr.Zero);

            if (code != 0) {
                throw new Win32Exception((int)code);
            }
        }

        private static string Digest(string path) {
            using (FileStream file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Write | FileShare.Delete))
            using (SHA256 digest = SHA256.Create()) {
                byte[] buffer = new byte[32768];
                int total = 0;
                int count;

                while ((count = file.Read(buffer, 0, buffer.Length)) > 0) {
                    total += count;

                    if (total > 4194304) {
                        throw new IOException("The remote file exceeds its byte bound");
                    }

                    digest.TransformBlock(buffer, 0, count, null, 0);
                }

                digest.TransformFinalBlock(new byte[0], 0, 0);

                return BitConverter.ToString(digest.Hash).Replace("-", "").ToLowerInvariant();
            }
        }

        private static void Create(string source, string destination) {
            IntPtr parent = IntPtr.Zero;
            IntPtr created = IntPtr.Zero;
            bool published = false;
            bool completed = false;

            using (SafeFileHandle file = CreateFileW(source, 0x00010000U | 0x00020000U | 0x00040000U, 7, IntPtr.Zero, 3, 0x00200000U, IntPtr.Zero)) {
                Check(!file.IsInvalid);

                try {
                    parent = Permissions(Path.GetDirectoryName(destination), 1 | 2 | 4);
                    SafeFileHandle token;
                    Check(OpenProcessToken(GetCurrentProcess(), 8, out token));

                    using (token) {
                        Mapping mapping = new Mapping();
                        mapping.Read = 0x00120089U;
                        mapping.Write = 0x00120116U;
                        mapping.Execute = 0x001200a0U;
                        mapping.All = 0x001f01ffU;
                        Check(CreatePrivateObjectSecurityEx(parent, IntPtr.Zero, out created, IntPtr.Zero, false, 1, token, ref mapping));
                    }

                    File.Move(source, destination);
                    published = true;
                    ApplyPermissions(file, created);
                    completed = true;
                }
                finally {
                    if (published && !completed) {
                        byte delete = 1;
                        SetFileInformationByHandle(file, 4, ref delete, 1);
                    }

                    if (created != IntPtr.Zero) {
                        DestroyPrivateObjectSecurity(ref created);
                    }

                    LocalFree(parent);
                }
            }
        }

        private static void Replace(string source, string destination) {
            IntPtr previous = Permissions(destination, 4);

            try {
                using (SafeFileHandle file = CreateFileW(source, 0x00020000U | 0x00040000U, 7, IntPtr.Zero, 3, 0x00200000U, IntPtr.Zero)) {
                    Check(!file.IsInvalid);
                    File.Replace(source, destination, null, false);
                    ApplyPermissions(file, previous);
                }
            }
            finally {
                LocalFree(previous);
            }
        }

        public static int Commit(string stage, string destination, string expected) {
            string payload = Path.Combine(stage, "payload");

            if (expected.Length == 0) {
                try {
                    Create(payload, destination);

                    return 0;
                }
                catch (IOException error) {
                    int code = error.HResult & 0xffff;

                    if (code == 80 || code == 183) {
                        return 10;
                    }

                    throw;
                }
            }

            if (!File.Exists(destination) || Digest(destination) != expected) {
                return 10;
            }

            Replace(payload, destination);

            return 0;
        }
    }
}
'@
$stage = $sshArguments[0]
$destination = $sshArguments[1]
$expected = $sshArguments[2]
$code = [Workpane.Ssh.FileWrites]::Commit($stage, $destination, $expected)
exit $code
