Add-Type -TypeDefinition @'
using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace Workpane.Ssh {
    public static class Programs {
        [StructLayout(LayoutKind.Sequential)]
        private struct SecurityAttributes {
            public uint Length;
            public IntPtr Descriptor;
            public int Inherit;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct Startup {
            public uint Size;
            public string Reserved;
            public string Desktop;
            public string Title;
            public uint X;
            public uint Y;
            public uint Width;
            public uint Height;
            public uint Columns;
            public uint Rows;
            public uint Fill;
            public uint Flags;
            public ushort Show;
            public ushort ReservedBytes;
            public IntPtr ReservedData;
            public IntPtr Input;
            public IntPtr Output;
            public IntPtr Error;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct StartupExtended {
            public Startup Startup;
            public IntPtr Attributes;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct ProcessInformation {
            public IntPtr Process;
            public IntPtr Thread;
            public uint ProcessId;
            public uint ThreadId;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct BasicLimits {
            public long ProcessTime;
            public long JobTime;
            public uint Flags;
            public UIntPtr MinimumWorkingSet;
            public UIntPtr MaximumWorkingSet;
            public uint ActiveProcesses;
            public UIntPtr Affinity;
            public uint Priority;
            public uint Scheduling;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct IoCounters {
            public ulong ReadOperations;
            public ulong WriteOperations;
            public ulong OtherOperations;
            public ulong ReadBytes;
            public ulong WriteBytes;
            public ulong OtherBytes;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct ExtendedLimits {
            public BasicLimits Basic;
            public IoCounters Io;
            public UIntPtr ProcessMemory;
            public UIntPtr JobMemory;
            public UIntPtr PeakProcessMemory;
            public UIntPtr PeakJobMemory;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CreatePipe(out IntPtr read, out IntPtr write, ref SecurityAttributes security, uint size);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetHandleInformation(IntPtr handle, uint mask, uint flags);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool InitializeProcThreadAttributeList(IntPtr attributes, int count, uint flags, ref UIntPtr size);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool UpdateProcThreadAttribute(IntPtr attributes, uint flags, UIntPtr kind, IntPtr value, UIntPtr size, IntPtr previous, IntPtr returned);

        [DllImport("kernel32.dll")]
        private static extern void DeleteProcThreadAttributeList(IntPtr attributes);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        private static extern bool CreateProcessW(string application, StringBuilder command, IntPtr processSecurity, IntPtr threadSecurity, bool inherit, uint flags, IntPtr environment, string directory, ref StartupExtended startup, out ProcessInformation process);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr CreateJobObjectW(IntPtr security, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetInformationJobObject(IntPtr job, int kind, ref ExtendedLimits limits, uint size);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateJobObject(IntPtr job, uint code);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateProcess(IntPtr process, uint code);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint ResumeThread(IntPtr thread);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetExitCodeProcess(IntPtr process, out uint code);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CancelIoEx(IntPtr handle, IntPtr operation);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);

        private static void Check(bool accepted) {
            if (!accepted) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
        }

        private static string Text(string value, int maximum, bool empty) {
            if (value == null || (!empty && value.Length == 0) || value.IndexOf('\0') >= 0 || new UTF8Encoding(false, true).GetByteCount(value) > maximum) {
                throw new ArgumentException("The SSH program declaration is invalid");
            }

            return value;
        }

        private static string Quote(string value) {
            StringBuilder result = new StringBuilder("\"");
            int slashes = 0;

            foreach (char character in value) {
                if (character == '\\') {
                    slashes++;
                    continue;
                }

                result.Append('\\', character == '"' ? slashes * 2 + 1 : slashes);
                result.Append(character);
                slashes = 0;
            }

            result.Append('\\', slashes * 2);
            result.Append('"');

            return result.ToString();
        }

        private static string EnvironmentBlock(IDictionary variables, string[] cleared) {
            SortedDictionary<string, string> values = new SortedDictionary<string, string>(StringComparer.OrdinalIgnoreCase);

            foreach (DictionaryEntry entry in Environment.GetEnvironmentVariables()) {
                values[(string)entry.Key] = (string)entry.Value;
            }

            if (variables.Count > 256 || cleared.Length > 256) {
                throw new ArgumentException("The SSH environment exceeds its bound");
            }

            foreach (string name in cleared) {
                Text(name, 256, false);

                if (name.IndexOf('=') >= 0) {
                    throw new ArgumentException("The SSH environment name is invalid");
                }

                values.Remove(name);
            }

            foreach (DictionaryEntry entry in variables) {
                string name = Text((string)entry.Key, 256, false);
                string value = Text((string)entry.Value, 65536, true);

                if (name.IndexOf('=') >= 0) {
                    throw new ArgumentException("The SSH environment name is invalid");
                }

                values[name] = value;
            }

            StringBuilder block = new StringBuilder();

            foreach (KeyValuePair<string, string> entry in values) {
                block.Append(entry.Key).Append('=').Append(entry.Value).Append('\0');
            }

            block.Append('\0');

            if (block.Length > 2097152) {
                throw new ArgumentException("The SSH environment exceeds its byte bound");
            }

            return block.ToString();
        }

        private static FileStream Stream(ref IntPtr handle, FileAccess access) {
            SafeFileHandle file = new SafeFileHandle(handle, true);
            handle = IntPtr.Zero;

            try {
                return new FileStream(file, access, access == FileAccess.Write ? 1 : 65536, false);
            }
            catch {
                file.Dispose();
                throw;
            }
        }

        private static async Task Input(Stream source, FileStream target, Func<bool> ended, CancellationToken cancellation) {
            try {
                await source.CopyToAsync(target, 65536, cancellation).ConfigureAwait(false);
            }
            catch (Exception error) {
                if (!ended() || !(error is IOException || error is OperationCanceledException)) {
                    throw;
                }
            }
            finally {
                target.Dispose();
            }
        }

        private static void Complete(string directory, uint code) {
            string temporary = Path.Combine(directory, "code.new");
            string destination = Path.Combine(directory, "code");
            string value = unchecked((int)code).ToString(System.Globalization.CultureInfo.InvariantCulture) + (code != 0 ? " 1\n" : " 0\n");
            File.WriteAllText(temporary, value, Encoding.ASCII);
            File.Move(temporary, destination);
        }

        public static void Fail(string directory) {
            Complete(directory, UInt32.MaxValue);
        }

        public static int Run(string executable, string[] arguments, string directory, IDictionary variables, string[] cleared, bool terminal, Stream input, FileStream output, FileStream error, string record, IntPtr inputHandle) {
            Text(executable, 4096, false);
            Text(directory, 4096, false);

            Text(record, 4096, false);

            if (!Path.IsPathRooted(executable) || !Path.IsPathRooted(directory) || !Path.IsPathRooted(record) || arguments.Length > 256) {
                throw new ArgumentException("The SSH program declaration is invalid");
            }

            StringBuilder command = new StringBuilder(Quote(executable));

            foreach (string argument in arguments) {
                command.Append(' ').Append(Quote(Text(argument, 65536, true)));
            }

            string extension = Path.GetExtension(executable);
            bool batch = extension.Equals(".cmd", StringComparison.OrdinalIgnoreCase) || extension.Equals(".bat", StringComparison.OrdinalIgnoreCase);

            if (batch) {
                char[] unsafePath = { '%', '!', '\r', '\n' };
                char[] unsafeArguments = { '"', '%', '!', '^', '&', '|', '<', '>', '(', ')', '\r', '\n' };

                if (executable.IndexOfAny(unsafePath) >= 0) {
                    throw new ArgumentException("The SSH batch program path is unsafe");
                }

                foreach (string argument in arguments) {
                    if (argument.IndexOfAny(unsafeArguments) >= 0) {
                        throw new ArgumentException("The SSH batch argument is unsafe");
                    }
                }

                executable = Path.Combine(Environment.SystemDirectory, "cmd.exe");
                command = new StringBuilder(Quote(executable) + " /d /v:off /s /c \"" + command.ToString() + "\"");
            }

            if (command.Length >= 32767) {
                throw new ArgumentException("The SSH command exceeds its native bound");
            }

            IntPtr environment = Marshal.StringToHGlobalUni(EnvironmentBlock(variables, cleared));
            IntPtr job = IntPtr.Zero;
            IntPtr attributes = IntPtr.Zero;
            IntPtr inherited = IntPtr.Zero;
            IntPtr[] pipes = new IntPtr[6];
            ProcessInformation started = new ProcessInformation();
            bool initialized = false;
            bool resumed = false;
            bool ended = false;
            ConsoleCancelEventHandler interrupt = delegate(object sender, ConsoleCancelEventArgs eventArgs) { eventArgs.Cancel = true; };

            try {
                job = CreateJobObjectW(IntPtr.Zero, null);
                Check(job != IntPtr.Zero);
                ExtendedLimits limits = new ExtendedLimits();
                limits.Basic.Flags = 0x2000;
                Check(SetInformationJobObject(job, 9, ref limits, (uint)Marshal.SizeOf(typeof(ExtendedLimits))));
                StartupExtended startup = new StartupExtended();
                startup.Startup.Size = (uint)Marshal.SizeOf(typeof(Startup));
                uint flags = 0x00000004U | 0x00000400U;

                if (!terminal) {
                    SecurityAttributes security = new SecurityAttributes();
                    security.Length = (uint)Marshal.SizeOf(typeof(SecurityAttributes));
                    security.Inherit = 1;

                    for (int index = 0; index < 6; index += 2) {
                        Check(CreatePipe(out pipes[index], out pipes[index + 1], ref security, 65536));
                        Check(SetHandleInformation(pipes[index == 0 ? 1 : index], 1, 0));
                    }

                    UIntPtr size = UIntPtr.Zero;
                    InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref size);
                    attributes = Marshal.AllocHGlobal(checked((int)size.ToUInt64()));
                    Check(InitializeProcThreadAttributeList(attributes, 1, 0, ref size));
                    initialized = true;
                    inherited = Marshal.AllocHGlobal(IntPtr.Size * 3);
                    Marshal.Copy(new IntPtr[] { pipes[0], pipes[3], pipes[5] }, 0, inherited, 3);
                    Check(UpdateProcThreadAttribute(attributes, 0, new UIntPtr(0x00020002U), inherited, new UIntPtr((uint)(IntPtr.Size * 3)), IntPtr.Zero, IntPtr.Zero));
                    startup.Startup.Size = (uint)Marshal.SizeOf(typeof(StartupExtended));
                    startup.Startup.Flags = 0x100;
                    startup.Startup.Input = pipes[0];
                    startup.Startup.Output = pipes[3];
                    startup.Startup.Error = pipes[5];
                    startup.Attributes = attributes;
                    flags |= 0x00080000U | 0x08000000U;
                }

                Check(CreateProcessW(executable, command, IntPtr.Zero, IntPtr.Zero, !terminal, flags, environment, directory, ref startup, out started));
                Check(AssignProcessToJobObject(job, started.Process));

                if (terminal) {
                    Console.CancelKeyPress += interrupt;
                    Check(ResumeThread(started.Thread) != UInt32.MaxValue);
                    resumed = true;
                    Check(WaitForSingleObject(started.Process, UInt32.MaxValue) == 0);
                    uint code;
                    Check(GetExitCodeProcess(started.Process, out code));
                    Check(TerminateJobObject(job, 1));
                    Console.Out.Flush();
                    Complete(record, code);

                    return unchecked((int)code);
                }

                foreach (int index in new int[] { 0, 3, 5 }) {
                    CloseHandle(pipes[index]);
                    pipes[index] = IntPtr.Zero;
                }

                using (FileStream standardInput = Stream(ref pipes[1], FileAccess.Write))
                using (FileStream standardOutput = Stream(ref pipes[2], FileAccess.Read))
                using (FileStream standardError = Stream(ref pipes[4], FileAccess.Read))
                using (CancellationTokenSource cancellation = new CancellationTokenSource()) {
                    Task writing = Input(input, standardInput, delegate { return Volatile.Read(ref ended); }, cancellation.Token);
                    Task readingOutput = standardOutput.CopyToAsync(output, 65536);
                    Task readingError = standardError.CopyToAsync(error, 65536);
                    uint code;

                    try {
                        Check(ResumeThread(started.Thread) != UInt32.MaxValue);
                        resumed = true;
                        Check(WaitForSingleObject(started.Process, UInt32.MaxValue) == 0);
                        Check(GetExitCodeProcess(started.Process, out code));
                    }
                    finally {
                        Volatile.Write(ref ended, true);
                        cancellation.Cancel();
                        Check(TerminateJobObject(job, 1));
                        CancelIoEx(inputHandle, IntPtr.Zero);
                        Task.WhenAll(writing, readingOutput, readingError).GetAwaiter().GetResult();
                    }

                    output.Flush();
                    error.Flush();
                    Complete(record, code);

                    return unchecked((int)code);
                }
            }
            finally {
                if (terminal) {
                    Console.CancelKeyPress -= interrupt;
                }

                if (job != IntPtr.Zero) {
                    CloseHandle(job);
                }

                if (!resumed && started.Process != IntPtr.Zero) {
                    TerminateProcess(started.Process, 1);
                }

                foreach (IntPtr handle in pipes) {
                    if (handle != IntPtr.Zero) {
                        CloseHandle(handle);
                    }
                }

                if (started.Thread != IntPtr.Zero) {
                    CloseHandle(started.Thread);
                }

                if (started.Process != IntPtr.Zero) {
                    CloseHandle(started.Process);
                }

                if (initialized) {
                    DeleteProcThreadAttributeList(attributes);
                }

                Marshal.FreeHGlobal(attributes);
                Marshal.FreeHGlobal(inherited);
                Marshal.FreeHGlobal(environment);
            }
        }
    }
}
'@
