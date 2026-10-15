Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Runtime.InteropServices;

namespace Workpane.Ssh {
    public sealed class Jobs : IDisposable {
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

        [StructLayout(LayoutKind.Sequential)]
        private struct Accounting {
            public long UserTime;
            public long KernelTime;
            public long PeriodUserTime;
            public long PeriodKernelTime;
            public uint Faults;
            public uint Total;
            public uint Active;
            public uint Terminated;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateJobObject(IntPtr attributes, string name);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr OpenJobObject(uint access, bool inherit, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetInformationJobObject(IntPtr job, int kind, ref ExtendedLimits limits, uint size);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateJobObject(IntPtr job, uint code);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool QueryInformationJobObject(IntPtr job, int kind, out Accounting information, uint size, IntPtr returned);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CancelIoEx(IntPtr handle, IntPtr operation);

        private readonly object gate = new object();
        private readonly Stream control;
        private readonly IntPtr inputHandle;
        private readonly CancellationTokenSource cancellation = new CancellationTokenSource();
        private readonly Timer lease;
        private readonly int leaseMilliseconds;
        private bool cleanupFailed;
        private readonly Dictionary<string, IntPtr> jobs = new Dictionary<string, IntPtr>();
        private readonly string prefix;
        private string highest;
        private bool closed;

        public Jobs(string nonce, Stream input, int leaseMilliseconds, IntPtr inputHandle) {
            if (!Identity(nonce) || input == null || leaseMilliseconds < 1000 || leaseMilliseconds > 120000) {
                throw new ArgumentException("The SSH scope identity is invalid");
            }

            prefix = "Local\\WorkpaneSsh." + nonce + ".";
            control = input;
            this.inputHandle = inputHandle;
            this.leaseMilliseconds = leaseMilliseconds;
            lease = new Timer(delegate { Dispose(); }, null, leaseMilliseconds, Timeout.Infinite);
        }

        private static bool Identity(string value) {
            return value != null && System.Text.RegularExpressions.Regex.IsMatch(value, "\\A[a-f0-9]{32}\\z");
        }

        public string Open(string identity) {
            lock (gate) {
                if (closed || !Identity(identity) || (highest != null && String.CompareOrdinal(identity, highest) <= 0) || jobs.Count >= 32) {
                    throw new InvalidOperationException("The SSH program cannot be admitted");
                }

                highest = identity;
                string name = prefix + identity;
                IntPtr job = CreateJobObject(IntPtr.Zero, name);
                int error = Marshal.GetLastWin32Error();

                if (job == IntPtr.Zero) {
                    throw new Win32Exception(error);
                }

                try {
                    if (error == 183) {
                        throw new InvalidOperationException("The SSH program identity already exists");
                    }

                    ExtendedLimits limits = new ExtendedLimits();
                    limits.Basic.Flags = 0x2000;

                    if (!SetInformationJobObject(job, 9, ref limits, (uint)Marshal.SizeOf(typeof(ExtendedLimits)))) {
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    }

                    jobs.Add(identity, job);
                    job = IntPtr.Zero;

                    return name;
                }
                finally {
                    if (job != IntPtr.Zero) {
                        CloseHandle(job);
                    }
                }
            }
        }

        public void Stop(string identity) {
            lock (gate) {
                if (closed || !Identity(identity)) {
                    throw new InvalidOperationException("The SSH program identity is invalid");
                }

                IntPtr job;

                if (highest == null || String.CompareOrdinal(identity, highest) > 0) {
                    highest = identity;
                }

                if (!jobs.TryGetValue(identity, out job)) {
                    return;
                }

                if (!TerminateJobObject(job, 1)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }

                Wait(job, Stopwatch.StartNew());

                if (!CloseHandle(job)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }

                jobs.Remove(identity);
            }
        }

        private static void Wait(IntPtr job, Stopwatch elapsed) {
            while (true) {
                Accounting information;

                if (!QueryInformationJobObject(job, 1, out information, (uint)Marshal.SizeOf(typeof(Accounting)), IntPtr.Zero)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }

                if (information.Active == 0) {
                    return;
                }

                if (elapsed.ElapsedMilliseconds >= 3000) {
                    throw new IOException("The SSH job cleanup exceeded its bound");
                }

                Thread.Sleep(1);
            }
        }

        public void Renew() {
            lock (gate) {
                if (closed) {
                    throw new IOException("The SSH scope has closed");
                }

                lease.Change(leaseMilliseconds, Timeout.Infinite);
            }
        }

        public string ReadControl() {
            byte[] buffer = new byte[1];
            System.Text.StringBuilder line = new System.Text.StringBuilder();

            while (line.Length < 16384) {
                Task<int> reading;

                lock (gate) {
                    if (closed) {
                        if (cleanupFailed) {
                            throw new IOException("The SSH scope cleanup failed");
                        }

                        return null;
                    }

                    reading = control.ReadAsync(buffer, 0, 1, cancellation.Token);
                }

                int count;

                try {
                    count = reading.GetAwaiter().GetResult();
                }
                catch (Exception error) {
                    lock (gate) {
                        if (!closed || cleanupFailed || !(error is OperationCanceledException || error is IOException)) {
                            throw;
                        }
                    }

                    return null;
                }

                if (count == 0) {
                    return null;
                }

                if (buffer[0] == 10) {
                    return line.ToString();
                }

                if (buffer[0] < 32 || buffer[0] > 126) {
                    throw new IOException("The SSH control input is invalid");
                }

                line.Append((char)buffer[0]);
            }

            throw new IOException("The SSH control input exceeds its bound");
        }

        public static void Join(string name) {
            if (name == null || !System.Text.RegularExpressions.Regex.IsMatch(name, "\\ALocal\\\\WorkpaneSsh\\.[a-f0-9]{32}\\.[a-f0-9]{32}\\z")) {
                throw new ArgumentException("The SSH job name is invalid");
            }

            IntPtr job = OpenJobObject(1, false, name);

            if (job == IntPtr.Zero) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

            try {
                using (Process process = Process.GetCurrentProcess()) {
                    if (!AssignProcessToJobObject(job, process.Handle)) {
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    }
                }
            }
            finally {
                CloseHandle(job);
            }
        }

        public void Dispose() {
            lock (gate) {
                if (closed) {
                    return;
                }

                closed = true;

                foreach (IntPtr job in jobs.Values) {
                    cleanupFailed |= !TerminateJobObject(job, 1);
                }

                Stopwatch elapsed = Stopwatch.StartNew();

                foreach (IntPtr job in jobs.Values) {
                    if (job != IntPtr.Zero) {
                        try {
                            Wait(job, elapsed);
                        }
                        catch {
                            cleanupFailed = true;
                        }

                        bool released = CloseHandle(job);
                        cleanupFailed |= !released;
                    }
                }

                jobs.Clear();
                lease.Dispose();
                cancellation.Cancel();
                CancelIoEx(inputHandle, IntPtr.Zero);
            }
        }
    }
}
'@
