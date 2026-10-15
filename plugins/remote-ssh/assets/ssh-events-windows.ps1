Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Workpane.Ssh {
    public sealed class Events : IDisposable {
        private readonly object gate = new object();
        private readonly string root;
        private readonly FileSystemWatcher watcher;
        private readonly TextWriter output;
        private readonly Action failed;
        private readonly HashSet<int> active = new HashSet<int>();
        private readonly HashSet<int> notified = new HashSet<int>();
        private bool closed;

        public Events(string root, TextWriter output, IDisposable owner) {
            this.root = Path.GetFullPath(root).TrimEnd('\\', '/');
            this.output = output;
            this.failed = owner.Dispose;
            watcher = new FileSystemWatcher(this.root);
            watcher.IncludeSubdirectories = true;
            watcher.NotifyFilter = NotifyFilters.FileName;
            watcher.InternalBufferSize = 65536;
            watcher.Created += delegate(object sender, FileSystemEventArgs args) { Receive(args.FullPath); };
            watcher.Renamed += delegate(object sender, RenamedEventArgs args) { Receive(args.FullPath); };
            watcher.Error += delegate { this.failed(); };
            watcher.EnableRaisingEvents = true;
        }

        public void Open(int identity) {
            lock (gate) {
                if (closed || identity < 1 || active.Count >= 32 || !active.Add(identity)) {
                    throw new InvalidOperationException("The SSH completion identity is invalid");
                }
            }
        }

        public void Stop(int identity) {
            lock (gate) {
                active.Remove(identity);
                notified.Remove(identity);
            }
        }

        private void Receive(string path) {
            lock (gate) {
                if (closed || !path.StartsWith(root + "\\", StringComparison.OrdinalIgnoreCase)) {
                    return;
                }

                string relative = path.Substring(root.Length + 1);
                string[] parts = relative.Split('\\');

                if (parts.Length != 2 || parts[1] != "code" || !parts[0].StartsWith("program.", StringComparison.Ordinal)) {
                    return;
                }

                int identity;

                if (!Int32.TryParse(parts[0].Substring(8), out identity) || !active.Contains(identity) || notified.Contains(identity)) {
                    return;
                }

                try {
                    string value;

                    using (FileStream file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete)) {
                        byte[] bytes = new byte[32];
                        int count = file.Read(bytes, 0, bytes.Length);

                        if (count == bytes.Length || file.ReadByte() >= 0) {
                            throw new IOException("The SSH completion exceeds its bound");
                        }

                        value = Encoding.ASCII.GetString(bytes, 0, count);
                    }

                    if (!System.Text.RegularExpressions.Regex.IsMatch(value, "\\A-?[0-9]{1,10} [01]\\n\\z")) {
                        throw new IOException("The SSH completion is invalid");
                    }

                    notified.Add(identity);
                    output.Write("DONE " + identity.ToString(System.Globalization.CultureInfo.InvariantCulture) + " " + value);
                    output.Flush();
                }
                catch {
                    failed();
                }
            }
        }

        public void Dispose() {
            lock (gate) {
                if (closed) {
                    return;
                }

                closed = true;
                watcher.Dispose();
                active.Clear();
                notified.Clear();
            }
        }
    }
}
'@
