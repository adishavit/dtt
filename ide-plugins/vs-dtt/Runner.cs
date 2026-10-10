using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace DTT
{
    /// <summary>
    /// DTT Runner - Executes clang with DTT plugin
    /// </summary>
    public sealed class Runner : IDisposable
    {
        public class Config
        {
            public string ClangPath { get; set; }
            public string PluginPath { get; set; }
            public string Standard { get; set; }
            public string Mode { get; set; }
            public string Content { get; set; }

            public Config(string clangPath, string pluginPath, string standard, string mode)
            {
                ClangPath = clangPath;
                PluginPath = pluginPath;
                Standard = standard;
                Mode = mode;
            }
        }

        public class Result
        {
            public int ExitCode { get; }
            public string Output { get; }

            public Result(int exitCode, string output)
            {
                ExitCode = exitCode;
                Output = output;
            }
        }

        private readonly ConcurrentDictionary<string, CancellationTokenSource> _running =
            new ConcurrentDictionary<string, CancellationTokenSource>();

        /// <summary>Run DTT analysis on a file</summary>
        public async Task<Result> RunAsync(string filePath, Config config, CancellationToken ct = default)
        {
            Cancel(filePath);

            using (var cts = CancellationTokenSource.CreateLinkedTokenSource(ct))
            {
                _running[filePath] = cts;
                cts.CancelAfter(TimeSpan.FromSeconds(30));
                string snapshot = null;

                try
                {
                    var args = new List<string>(BuildArgsList(filePath, config));
                    if (config.Content != null)
                    {
                        snapshot = Path.Combine(Path.GetTempPath(), "dtt-editor-" + Guid.NewGuid());
                        Directory.CreateDirectory(snapshot);
                        var source = Path.Combine(snapshot, "source.cpp");
                        var overlay = Path.Combine(snapshot, "overlay.json");
                        File.WriteAllText(source, config.Content);
                        File.WriteAllText(overlay, "{\"version\":0,\"use-external-names\":false,\"roots\":[{\"type\":\"file\",\"name\":" +
                            JsonString(Path.GetFullPath(filePath)) + ",\"external-contents\":" + JsonString(source) + "}]}");
                        args.InsertRange(0, new[] { "-ivfsoverlay", overlay });
                    }
                    return await ExecuteAsync(config.ClangPath, args.ToArray(), cts.Token);
                }
                catch (Exception ex)
                {
                    return new Result(-1, "DTT: Analysis failed - " + ex.Message);
                }
                finally
                {
                    ((ICollection<KeyValuePair<string, CancellationTokenSource>>)_running)
                        .Remove(new KeyValuePair<string, CancellationTokenSource>(filePath, cts));
                    if (snapshot != null) Directory.Delete(snapshot, true);
                }
            }
        }

        public static async Task<bool> IsCompatibleClangAsync(string path)
        {
            using (var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5)))
            {
                var result = await ExecuteAsync(path, new[] { "--version" }, timeout.Token);
                return result.ExitCode == 0 && Regex.IsMatch(result.Output, @"clang version 18[.\s]") &&
                    !result.Output.Contains("Apple clang");
            }
        }

        public async Task<bool> ProbeAsync(Config config)
        {
            if (!await IsCompatibleClangAsync(config.ClangPath)) return false;
            var source = Path.Combine(Path.GetTempPath(), "dtt-probe-" + Guid.NewGuid() + ".cpp");
            try
            {
                File.WriteAllText(source, "void check(){int *p=nullptr; *p=1;}");
                var result = await RunAsync(source, new Config(config.ClangPath, config.PluginPath, "c++20", "quiet"));
                return result.ExitCode == 1 && Regex.IsMatch(result.Output, "error: DTT:.*null pointer");
            }
            finally { File.Delete(source); }
        }

        private static string JsonString(string value)
        {
            var result = new StringBuilder("\"");
            foreach (var c in value)
            {
                if (c == '\\' || c == '\"') result.Append('\\').Append(c);
                else if (c < 32) result.Append("\\u").Append(((int)c).ToString("x4"));
                else result.Append(c);
            }
            return result.Append('\"').ToString();
        }

        /// <summary>Cancel a running analysis</summary>
        public void Cancel(string filePath)
        {
            CancellationTokenSource cts;
            if (_running.TryRemove(filePath, out cts))
                cts.Cancel();
        }

        /// <summary>Cancel all running analyses</summary>
        public void Dispose()
        {
            foreach (var kvp in _running)
                kvp.Value.Cancel();
            _running.Clear();
        }

        /// <summary>Build arguments as a list to avoid shell injection</summary>
        private static string[] BuildArgsList(string filePath, Config config)
        {
            var args = new System.Collections.Generic.List<string>
            {
                $"-fplugin={config.PluginPath}",
                $"-std={config.Standard}",
                "-fsyntax-only",
                "-fno-color-diagnostics"
            };

            if (Environment.OSVersion.Platform == PlatformID.Win32NT)
            {
                args.RemoveAt(0);
                args.InsertRange(0, new[] { "-Xclang", "-load", "-Xclang", config.PluginPath,
                    "-Xclang", "-add-plugin", "-Xclang", "dtt" });
                if (config.Mode != "inform") args.AddRange(new[] { "-Xclang", "-plugin-arg-dtt", "-Xclang", "--dtt-mode=" + config.Mode });
            }
            else if (config.Mode != "inform") args.Add($"-fplugin-arg-dtt---dtt-mode={config.Mode}");

            args.Add(filePath);
            return args.ToArray();
        }

        /// <summary>Escape a single argument for Windows command line</summary>
        private static string EscapeArgument(string arg)
        {
            if (string.IsNullOrEmpty(arg))
                return "\"\"";

            // If no special characters, return as-is
            if (arg.IndexOfAny(new[] { ' ', '\t', '"', '\\' }) < 0)
                return arg;

            // Escape backslashes before quotes, then escape quotes, then wrap in quotes
            var sb = new System.Text.StringBuilder();
            sb.Append('"');
            int backslashes = 0;
            foreach (char c in arg)
            {
                if (c == '\\')
                {
                    backslashes++;
                }
                else if (c == '"')
                {
                    // Double the backslashes before a quote, then escape the quote
                    sb.Append('\\', backslashes * 2 + 1);
                    sb.Append('"');
                    backslashes = 0;
                }
                else
                {
                    sb.Append('\\', backslashes);
                    sb.Append(c);
                    backslashes = 0;
                }
            }
            // Handle trailing backslashes
            sb.Append('\\', backslashes * 2);
            sb.Append('"');
            return sb.ToString();
        }

        private static async Task<Result> ExecuteAsync(string exe, string[] args, CancellationToken ct)
        {
            using (var proc = new Process())
            {
                // Build escaped argument string for .NET Framework compatibility
                var escapedArgs = string.Join(" ", System.Linq.Enumerable.Select(args, EscapeArgument));

                proc.StartInfo = new ProcessStartInfo
                {
                    FileName = exe,
                    Arguments = escapedArgs,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true
                };

                var stdout = new System.Text.StringBuilder();
                var stderr = new System.Text.StringBuilder();
                var outputDone = new System.Threading.Tasks.TaskCompletionSource<bool>();
                var errorDone = new System.Threading.Tasks.TaskCompletionSource<bool>();

                proc.OutputDataReceived += (s, e) =>
                {
                    if (e.Data == null) outputDone.TrySetResult(true);
                    else stdout.AppendLine(e.Data);
                };
                proc.ErrorDataReceived += (s, e) =>
                {
                    if (e.Data == null) errorDone.TrySetResult(true);
                    else stderr.AppendLine(e.Data);
                };

                try
                {
                    proc.Start();
                    proc.BeginOutputReadLine();
                    proc.BeginErrorReadLine();

                    // Register cancellation to kill process
                    using (ct.Register(() =>
                    {
                        try { if (!proc.HasExited) proc.Kill(); } catch { }
                    }))
                    {
                        await Task.WhenAll(outputDone.Task, errorDone.Task);
                        await Task.Run(() => proc.WaitForExit(), ct);
                    }

                    if (ct.IsCancellationRequested)
                    {
                        return new Result(-1, "Cancelled");
                    }

                    return new Result(proc.ExitCode, stdout.ToString() + stderr.ToString());
                }
                catch (OperationCanceledException)
                {
                    try { if (!proc.HasExited) proc.Kill(); } catch { }
                    return new Result(-1, "Cancelled");
                }
                catch (Exception ex)
                {
                    try { if (!proc.HasExited) proc.Kill(); } catch { }
                    return new Result(-1, ex.Message);
                }
            }
        }
    }
}
