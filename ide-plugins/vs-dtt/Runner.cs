using System;
using System.Collections.Concurrent;
using System.Diagnostics;
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

                try
                {
                    var args = BuildArgs(filePath, config);
                    return await ExecuteAsync(config.ClangPath, args, cts.Token);
                }
                finally
                {
                    CancellationTokenSource ignored;
                    _running.TryRemove(filePath, out ignored);
                }
            }
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

        private static string BuildArgs(string filePath, Config config)
        {
            var mode = config.Mode != "inform"
                ? $"-fplugin-arg-dtt---dtt-mode={config.Mode}"
                : "";

            return string.Join(" ",
                $"-fplugin=\"{config.PluginPath}\"",
                $"-std={config.Standard}",
                "-fsyntax-only",
                "-fno-color-diagnostics",
                mode,
                $"\"{filePath}\""
            ).Trim();
        }

        private static Task<Result> ExecuteAsync(string exe, string args, CancellationToken ct)
        {
            return Task.Run(() =>
            {
                using (var proc = new Process())
                {
                    proc.StartInfo = new ProcessStartInfo
                    {
                        FileName = exe,
                        Arguments = args,
                        UseShellExecute = false,
                        RedirectStandardOutput = true,
                        RedirectStandardError = true,
                        CreateNoWindow = true
                    };

                    try
                    {
                        proc.Start();
                        var stdout = proc.StandardOutput.ReadToEnd();
                        var stderr = proc.StandardError.ReadToEnd();
                        proc.WaitForExit();

                        if (ct.IsCancellationRequested)
                        {
                            try { proc.Kill(); } catch { }
                            return new Result(-1, "Cancelled");
                        }

                        return new Result(proc.ExitCode, stdout + stderr);
                    }
                    catch (OperationCanceledException)
                    {
                        try { proc.Kill(); } catch { }
                        return new Result(-1, "Cancelled");
                    }
                    catch (Exception ex)
                    {
                        return new Result(-1, ex.Message);
                    }
                }
            }, ct);
        }
    }
}
