using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace DTTAnalyzer
{
    /// <summary>
    /// Service for running DTT analysis and managing diagnostics
    /// </summary>
    public class DTTService
    {
        private static readonly Lazy<DTTService> _instance = new(() => new DTTService());
        public static DTTService Instance => _instance.Value;

        private readonly Dictionary<string, List<DTTDiagnostic>> _diagnostics = new();
        private readonly object _lock = new();
        private IVsErrorList? _errorList;
        private ErrorListProvider? _errorListProvider;

        private DTTService() { }

        public async Task InitializeAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            var serviceProvider = await VS.GetServiceAsync<SVsServiceProvider, IServiceProvider>();
            if (serviceProvider != null)
            {
                _errorListProvider = new ErrorListProvider(serviceProvider);
            }
        }

        /// <summary>
        /// Analyze a single file
        /// </summary>
        public async Task<List<DTTDiagnostic>> AnalyzeFileAsync(string filePath, CancellationToken cancellationToken = default)
        {
            var options = await GeneralOptions.GetLiveInstanceAsync();

            if (string.IsNullOrEmpty(options.PluginPath))
            {
                await VS.MessageBox.ShowErrorAsync("DTT Error",
                    "DTT plugin path is not configured. Please set it in Tools > Options > DTT > General.");
                return new List<DTTDiagnostic>();
            }

            await OutputWindow.WriteLineAsync($"[DTT] Analyzing: {filePath}");

            var command = BuildCommand(options, filePath);
            await OutputWindow.WriteLineAsync($"[DTT] Command: {command}");

            var (exitCode, output) = await RunCommandAsync(command, options, cancellationToken);

            var diagnostics = ParseDiagnostics(output, filePath);

            lock (_lock)
            {
                _diagnostics[filePath] = diagnostics;
            }

            await UpdateErrorListAsync(filePath, diagnostics);

            var errors = diagnostics.Count(d => d.Severity == DiagnosticSeverity.Error);
            var warnings = diagnostics.Count(d => d.Severity == DiagnosticSeverity.Warning);
            await OutputWindow.WriteLineAsync($"[DTT] Analysis complete: {errors} errors, {warnings} warnings");

            return diagnostics;
        }

        /// <summary>
        /// Analyze all C++ files in the solution
        /// </summary>
        public async Task AnalyzeSolutionAsync(IProgress<string>? progress = null, CancellationToken cancellationToken = default)
        {
            var solution = await VS.Solutions.GetCurrentSolutionAsync();
            if (solution == null)
            {
                await VS.MessageBox.ShowWarningAsync("DTT", "No solution is currently open.");
                return;
            }

            var cppFiles = new List<string>();
            await CollectCppFilesAsync(solution, cppFiles);

            if (cppFiles.Count == 0)
            {
                await VS.MessageBox.ShowAsync("DTT", "No C++ files found in the solution.");
                return;
            }

            await OutputWindow.WriteLineAsync($"[DTT] Analyzing {cppFiles.Count} files...");

            int processed = 0;
            foreach (var file in cppFiles)
            {
                if (cancellationToken.IsCancellationRequested)
                    break;

                progress?.Report($"Analyzing {Path.GetFileName(file)} ({++processed}/{cppFiles.Count})");
                await AnalyzeFileAsync(file, cancellationToken);
            }

            await OutputWindow.WriteLineAsync($"[DTT] Solution analysis complete");
        }

        private async Task CollectCppFilesAsync(SolutionItem item, List<string> files)
        {
            if (item is Solution solution)
            {
                foreach (var project in await solution.GetAllProjectsAsync())
                {
                    await CollectCppFilesFromProjectAsync(project, files);
                }
            }
        }

        private async Task CollectCppFilesFromProjectAsync(Project project, List<string> files)
        {
            try
            {
                var children = await project.GetFilesAsync();
                foreach (var child in children)
                {
                    var path = child.FullPath;
                    if (!string.IsNullOrEmpty(path) && DTTAnalyzerPackage.IsCppFile(path) && File.Exists(path))
                    {
                        files.Add(path);
                    }
                }
            }
            catch
            {
                // Ignore errors from inaccessible projects
            }
        }

        /// <summary>
        /// Clear all diagnostics
        /// </summary>
        public async Task ClearDiagnosticsAsync()
        {
            lock (_lock)
            {
                _diagnostics.Clear();
            }

            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            _errorListProvider?.Tasks.Clear();

            await OutputWindow.WriteLineAsync("[DTT] Diagnostics cleared");
        }

        private string BuildCommand(GeneralOptions options, string filePath)
        {
            var args = new List<string>
            {
                options.ClangPath,
                $"-fplugin={options.PluginPath}",
                $"-std={GetCppStandard(options.Standard)}",
                "-fsyntax-only",
                "-fno-color-diagnostics"
            };

            if (options.Mode != DTTMode.Inform)
            {
                args.Add($"-fplugin-arg-dtt---dtt-mode={options.Mode.ToString().ToLower()}");
            }

            if (!string.IsNullOrWhiteSpace(options.IncludePaths))
            {
                foreach (var path in options.IncludePaths.Split(';', StringSplitOptions.RemoveEmptyEntries))
                {
                    args.Add($"-I{path.Trim()}");
                }
            }

            if (!string.IsNullOrWhiteSpace(options.AdditionalFlags))
            {
                args.Add(options.AdditionalFlags);
            }

            // Convert Windows path to WSL path if using WSL
            var analysisPath = options.UseWsl ? ConvertToWslPath(filePath) : filePath;
            args.Add($"\"{analysisPath}\"");

            return string.Join(" ", args);
        }

        private static string GetCppStandard(CppStandard standard) => standard switch
        {
            CppStandard.Cpp17 => "c++17",
            CppStandard.Cpp20 => "c++20",
            CppStandard.Cpp23 => "c++23",
            _ => "c++20"
        };

        private static string ConvertToWslPath(string windowsPath)
        {
            if (string.IsNullOrEmpty(windowsPath))
                return windowsPath;

            // Convert C:\path\to\file to /mnt/c/path/to/file
            if (windowsPath.Length >= 2 && windowsPath[1] == ':')
            {
                var driveLetter = char.ToLower(windowsPath[0]);
                var rest = windowsPath.Substring(2).Replace('\\', '/');
                return $"/mnt/{driveLetter}{rest}";
            }

            return windowsPath.Replace('\\', '/');
        }

        private async Task<(int exitCode, string output)> RunCommandAsync(string command, GeneralOptions options, CancellationToken cancellationToken)
        {
            var psi = new ProcessStartInfo
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };

            if (options.UseWsl)
            {
                psi.FileName = "wsl";
                if (!string.IsNullOrEmpty(options.WslDistribution))
                {
                    psi.Arguments = $"-d {options.WslDistribution} -- {command}";
                }
                else
                {
                    psi.Arguments = $"-- {command}";
                }
            }
            else
            {
                psi.FileName = "cmd.exe";
                psi.Arguments = $"/c {command}";
            }

            try
            {
                using var process = Process.Start(psi);
                if (process == null)
                {
                    return (-1, "Failed to start process");
                }

                var stdout = await process.StandardOutput.ReadToEndAsync();
                var stderr = await process.StandardError.ReadToEndAsync();

                await process.WaitForExitAsync(cancellationToken);

                return (process.ExitCode, stdout + stderr);
            }
            catch (Exception ex)
            {
                return (-1, $"Error running DTT: {ex.Message}");
            }
        }

        private static List<DTTDiagnostic> ParseDiagnostics(string output, string currentFile)
        {
            var diagnostics = new List<DTTDiagnostic>();
            var lines = output.Split('\n');

            // Match clang diagnostic format: file:line:column: severity: message
            var regex = new Regex(@"^(.+?):(\d+):(\d+):\s+(error|warning|note):\s+(.+)$");

            foreach (var line in lines)
            {
                var match = regex.Match(line.Trim());
                if (!match.Success)
                    continue;

                var file = match.Groups[1].Value;
                var lineNum = int.Parse(match.Groups[2].Value);
                var column = int.Parse(match.Groups[3].Value);
                var severityStr = match.Groups[4].Value;
                var message = match.Groups[5].Value;

                // Only include DTT diagnostics
                if (!message.Contains("DTT:") && !message.Contains("dtt:"))
                {
                    if (severityStr != "note")
                        continue;
                }

                var severity = severityStr switch
                {
                    "error" => DiagnosticSeverity.Error,
                    "warning" => DiagnosticSeverity.Warning,
                    _ => DiagnosticSeverity.Information
                };

                // Extract category if present
                string? category = null;
                var categoryMatch = Regex.Match(message, @"\[Category ([A-G])\]");
                if (categoryMatch.Success)
                {
                    category = categoryMatch.Groups[1].Value;
                }

                // Convert WSL path back to Windows path if necessary
                var normalizedFile = NormalizePath(file);

                diagnostics.Add(new DTTDiagnostic
                {
                    FilePath = normalizedFile,
                    Line = lineNum,
                    Column = column,
                    Severity = severity,
                    Message = message,
                    Category = category
                });
            }

            return diagnostics;
        }

        private static string NormalizePath(string path)
        {
            // Convert WSL path /mnt/c/... back to C:\...
            if (path.StartsWith("/mnt/") && path.Length >= 6)
            {
                var driveLetter = char.ToUpper(path[5]);
                var rest = path.Substring(6).Replace('/', '\\');
                return $"{driveLetter}:{rest}";
            }
            return path;
        }

        private async Task UpdateErrorListAsync(string filePath, List<DTTDiagnostic> diagnostics)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            if (_errorListProvider == null)
                return;

            // Remove existing errors for this file
            var tasksToRemove = _errorListProvider.Tasks.Cast<ErrorTask>()
                .Where(t => string.Equals(t.Document, filePath, StringComparison.OrdinalIgnoreCase))
                .ToList();

            foreach (var task in tasksToRemove)
            {
                _errorListProvider.Tasks.Remove(task);
            }

            // Add new errors
            foreach (var diag in diagnostics)
            {
                var task = new ErrorTask
                {
                    Document = diag.FilePath,
                    Line = diag.Line - 1,  // Error list uses 0-based line numbers
                    Column = diag.Column - 1,
                    Text = diag.Message,
                    ErrorCategory = diag.Severity switch
                    {
                        DiagnosticSeverity.Error => TaskErrorCategory.Error,
                        DiagnosticSeverity.Warning => TaskErrorCategory.Warning,
                        _ => TaskErrorCategory.Message
                    },
                    Category = TaskCategory.BuildCompile
                };

                task.Navigate += (sender, e) =>
                {
                    var errorTask = sender as ErrorTask;
                    if (errorTask != null)
                    {
                        _errorListProvider.Navigate(errorTask, Guid.Empty);
                    }
                };

                _errorListProvider.Tasks.Add(task);
            }

            _errorListProvider.Show();
        }
    }

    /// <summary>
    /// Represents a DTT diagnostic
    /// </summary>
    public class DTTDiagnostic
    {
        public string FilePath { get; set; } = "";
        public int Line { get; set; }
        public int Column { get; set; }
        public DiagnosticSeverity Severity { get; set; }
        public string Message { get; set; } = "";
        public string? Category { get; set; }
    }

    /// <summary>
    /// Diagnostic severity levels
    /// </summary>
    public enum DiagnosticSeverity
    {
        Error,
        Warning,
        Information
    }

    /// <summary>
    /// Helper for writing to the VS output window
    /// </summary>
    internal static class OutputWindow
    {
        private static OutputWindowPane? _pane;

        public static async Task WriteLineAsync(string message)
        {
            try
            {
                _pane ??= await VS.Windows.CreateOutputWindowPaneAsync("DTT", lazyCreate: true);
                await _pane.WriteLineAsync(message);
            }
            catch
            {
                // Ignore output window errors
            }
        }
    }
}
