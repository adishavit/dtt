using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

namespace DTT;

/// <summary>
/// DTT Runner - Executes clang with DTT plugin
/// </summary>
public sealed class Runner : IDisposable
{
    public record Config(
        string ClangPath,
        string PluginPath,
        string Standard,
        string Mode
    );

    public record Result(int ExitCode, string Output);

    private readonly ConcurrentDictionary<string, CancellationTokenSource> _running = new();

    /// <summary>Run DTT analysis on a file</summary>
    public async Task<Result> RunAsync(string filePath, Config config, CancellationToken ct = default)
    {
        Cancel(filePath);

        using var cts = CancellationTokenSource.CreateLinkedTokenSource(ct);
        _running[filePath] = cts;

        try
        {
            var args = BuildArgs(filePath, config);
            return await ExecuteAsync(config.ClangPath, args, cts.Token);
        }
        finally
        {
            _running.TryRemove(filePath, out _);
        }
    }

    /// <summary>Cancel a running analysis</summary>
    public void Cancel(string filePath)
    {
        if (_running.TryRemove(filePath, out var cts))
            cts.Cancel();
    }

    /// <summary>Cancel all running analyses</summary>
    public void Dispose()
    {
        foreach (var (_, cts) in _running)
            cts.Cancel();
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

    private static async Task<Result> ExecuteAsync(string exe, string args, CancellationToken ct)
    {
        using var proc = new Process
        {
            StartInfo = new ProcessStartInfo
            {
                FileName = exe,
                Arguments = args,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            }
        };

        try
        {
            proc.Start();
            var stdout = await proc.StandardOutput.ReadToEndAsync();
            var stderr = await proc.StandardError.ReadToEndAsync();
            await proc.WaitForExitAsync(ct);
            return new Result(proc.ExitCode, stdout + stderr);
        }
        catch (OperationCanceledException)
        {
            proc.Kill();
            return new Result(-1, "Cancelled");
        }
        catch (Exception ex)
        {
            return new Result(-1, ex.Message);
        }
    }
}
