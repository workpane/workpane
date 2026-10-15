Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace Workpane.Ssh {
    public static class Paths {
        [StructLayout(LayoutKind.Sequential)]
        private struct SecurityAttributes {
            public uint Length;
            public IntPtr Descriptor;
            public int Inherit;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct FileInformation {
            public uint Attributes;
            public uint CreatedLow;
            public uint CreatedHigh;
            public uint AccessedLow;
            public uint AccessedHigh;
            public uint ModifiedLow;
            public uint ModifiedHigh;
            public uint Volume;
            public uint SizeHigh;
            public uint SizeLow;
            public uint Links;
            public uint IndexHigh;
            public uint IndexLow;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct FileIdentity {
            public ulong Volume;
            public ulong Low;
            public ulong High;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(SafeFileHandle file, StringBuilder path, uint capacity, uint flags);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(SafeFileHandle file, out FileInformation information);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandleEx(SafeFileHandle file, int kind, out FileIdentity identity, uint size);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        private static extern bool CreateDirectoryW(string path, ref SecurityAttributes security);

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        private static extern bool ConvertStringSecurityDescriptorToSecurityDescriptorW(string declaration, uint revision, out IntPtr descriptor, out uint size);

        [DllImport("kernel32.dll")]
        private static extern IntPtr LocalFree(IntPtr memory);

        public static string Find(string program, string[] additional) {
            if (String.IsNullOrEmpty(program) || program.Length > 4096 || program.IndexOf('\0') >= 0 || additional.Length > 256) {
                throw new ArgumentException("The SSH program lookup is invalid");
            }

            if (Path.IsPathRooted(program)) {
                return Executable(Absolute(program));
            }

            if (program.IndexOf('/') >= 0 || program.IndexOf('\\') >= 0) {
                return null;
            }

            List<string> directories = new List<string>();
            string environment = Environment.GetEnvironmentVariable("PATH");

            if (environment != null) {
                foreach (string directory in environment.Split(';')) {
                    if (directory.Length > 0) {
                        directories.Add(directory);
                    }
                }
            }

            directories.AddRange(additional);

            foreach (string directory in directories) {
                string found = Executable(Path.Combine(directory, program));

                if (found != null) {
                    return found;
                }
            }

            return null;
        }

        private static string Executable(string candidate) {
            string[] extensions = { ".exe", ".cmd", ".bat", ".com" };
            string extension = Path.GetExtension(candidate).ToLowerInvariant();

            foreach (string runnable in extensions) {
                if (extension == runnable) {
                    return File.Exists(candidate) ? Path.GetFullPath(candidate).Replace('\\', '/') : null;
                }
            }

            foreach (string runnable in extensions) {
                string extended = candidate + runnable;

                if (File.Exists(extended)) {
                    return Path.GetFullPath(extended).Replace('\\', '/');
                }
            }

            return null;
        }

        public static void CreatePrivate(string path) {
            path = Absolute(path);
            string owner;

            using (WindowsIdentity identity = WindowsIdentity.GetCurrent()) {
                owner = identity.User.Value;
            }

            SecurityAttributes security = new SecurityAttributes();
            security.Length = (uint)Marshal.SizeOf(typeof(SecurityAttributes));
            uint size;

            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW("O:" + owner + "D:P(A;OICI;FA;;;" + owner + ")", 1, out security.Descriptor, out size)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

            try {
                if (!CreateDirectoryW(path, ref security)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
            }
            finally {
                LocalFree(security.Descriptor);
            }
        }

        public static string CreateScope() {
            string path = Path.Combine(Path.GetTempPath(), "workpane-ssh-" + Guid.NewGuid().ToString("N"));
            CreatePrivate(path);

            return Absolute(path).Replace('\\', '/');
        }

        public static string Metadata(string path) {
            path = Absolute(path);
            FileAttributes attributes = File.GetAttributes(path);
            bool directory = (attributes & FileAttributes.Directory) != 0;
            string kind = directory ? "directory" : "file";
            long size = directory ? 0 : new FileInfo(path).Length;
            DateTime modified = File.GetLastWriteTimeUtc(path);
            bool recent = modified >= DateTime.UtcNow.AddSeconds(-2);
            bool writable = directory || (attributes & FileAttributes.ReadOnly) == 0;

            return kind + " " + size.ToString(System.Globalization.CultureInfo.InvariantCulture) + " " + modified.Ticks.ToString(System.Globalization.CultureInfo.InvariantCulture) + (recent ? " 1 1" : " 0 1") + (writable ? " 1" : " 0");
        }

        private static string Absolute(string value) {
            if (String.IsNullOrEmpty(value) || value.IndexOf('\0') >= 0 || new UTF8Encoding(false, true).GetByteCount(value) > 4096) {
                throw new ArgumentException("The SSH filesystem path is invalid");
            }

            string path = value.Replace('/', '\\');
            bool drive = path.Length >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' && path[2] == '\\';
            bool share = path.StartsWith(@"\\", StringComparison.Ordinal) && !path.StartsWith(@"\\?\", StringComparison.Ordinal) && !path.StartsWith(@"\\.\", StringComparison.Ordinal);

            if (!drive && !share) {
                throw new ArgumentException("The SSH filesystem path is not absolute");
            }

            string result = Path.GetFullPath(path);
            string root = Path.GetPathRoot(result);

            if (root == null || (share && root.TrimEnd('\\').Split('\\').Length != 4)) {
                throw new ArgumentException("The SSH filesystem share path is invalid");
            }

            return result;
        }

        private static SafeFileHandle Open(string path, bool link) {
            return CreateFileW(path, 0, 7, IntPtr.Zero, 3, link ? 0x02200000U : 0x02000000U, IntPtr.Zero);
        }

        private static FileIdentity Identity(SafeFileHandle file) {
            FileIdentity value;

            if (!GetFileInformationByHandleEx(file, 18, out value, (uint)Marshal.SizeOf(typeof(FileIdentity)))) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

            return value;
        }

        private static bool Same(FileIdentity first, FileIdentity second) {
            return first.Volume == second.Volume && first.Low == second.Low && first.High == second.High;
        }

        private static bool Directory(SafeFileHandle file) {
            FileInformation value;

            if (!GetFileInformationByHandle(file, out value)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

            return (value.Attributes & 16) != 0;
        }

        private static string Final(SafeFileHandle file) {
            StringBuilder value = new StringBuilder(4097);
            uint count = GetFinalPathNameByHandleW(file, value, (uint)value.Capacity, 0);

            if (count == 0) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

            if (count >= value.Capacity) {
                throw new IOException("The SSH filesystem path exceeds its bound");
            }

            string path = value.ToString();

            if (path.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase)) {
                path = @"\\" + path.Substring(8);
            }
            else if (path.StartsWith(@"\\?\", StringComparison.Ordinal)) {
                path = path.Substring(4);
            }

            return Absolute(path);
        }

        private static string Existing(string path, bool missing, out FileIdentity identity, out bool directory, out string ancestor) {
            List<string> suffix = new List<string>();

            for (int depth = 0; depth < 256; depth++) {
                using (SafeFileHandle file = Open(path, false)) {
                    int error = Marshal.GetLastWin32Error();

                    if (!file.IsInvalid) {
                        directory = Directory(file);
                        identity = Identity(file);
                        string resolved = Final(file);
                        ancestor = resolved;

                        if (suffix.Count != 0 && !directory) {
                            throw new IOException("The SSH filesystem ancestor is not a directory");
                        }

                        foreach (string segment in suffix) {
                            resolved = Path.Combine(resolved, segment);
                        }

                        return Absolute(resolved);
                    }

                    if (!missing || (error != 2 && error != 3)) {
                        throw new Win32Exception(error);
                    }

                    using (SafeFileHandle entry = Open(path, true)) {
                        int inspected = Marshal.GetLastWin32Error();

                        if (!entry.IsInvalid || (inspected != 2 && inspected != 3)) {
                            throw new Win32Exception(entry.IsInvalid ? inspected : error);
                        }
                    }
                }

                string name = Path.GetFileName(path);
                string parent = Path.GetDirectoryName(path);

                if (String.IsNullOrEmpty(name) || String.IsNullOrEmpty(parent)) {
                    throw new IOException("The SSH filesystem path has no readable ancestor");
                }

                suffix.Insert(0, name);
                path = parent;
            }

            throw new IOException("The SSH filesystem ancestor count exceeds its bound");
        }

        public static string Resolve(string root, string path, bool missing) {
            root = Absolute(root);
            path = Absolute(path);

            using (SafeFileHandle workspace = Open(root, false)) {
                if (workspace.IsInvalid) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }

                if (!Directory(workspace)) {
                    throw new IOException("The SSH workspace is not a directory");
                }

                FileIdentity owner = Identity(workspace);
                FileIdentity target;
                bool directory;
                string ancestorPath;
                string resolved = Existing(path, missing, out target, out directory, out ancestorPath);

                if (Same(owner, target)) {
                    return resolved.Replace('\\', '/');
                }

                string parent = directory ? ancestorPath : Path.GetDirectoryName(ancestorPath);

                for (int depth = 0; depth < 256 && !String.IsNullOrEmpty(parent); depth++) {
                    using (SafeFileHandle ancestor = Open(parent, false)) {
                        if (ancestor.IsInvalid) {
                            throw new Win32Exception(Marshal.GetLastWin32Error());
                        }

                        if (Same(owner, Identity(ancestor))) {
                            return resolved.Replace('\\', '/');
                        }

                        parent = Path.GetDirectoryName(Final(ancestor));
                    }
                }
            }

            throw new UnauthorizedAccessException("The SSH filesystem path leaves its workspace");
        }
    }
}
'@
