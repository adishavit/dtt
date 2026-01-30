using System;
using System.Collections.Concurrent;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Task = System.Threading.Tasks.Task;

namespace DTT;

[PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
[Guid("a1b2c3d4-e5f6-7890-abcd-ef1234567890")]
[ProvideAutoLoad(VSConstants.UICONTEXT.SolutionExists_string, PackageAutoLoadFlags.BackgroundLoad)]
[ProvideOptionPage(typeof(Options), "DTT", "General", 0, 0, true)]
[ProvideMenuResource("Menus.ctmenu", 1)]
public sealed class DTTPackage : ToolkitPackage
{
    public static DTTPackage? Instance { get; private set; }
    public Options Settings => GetDialogPage(typeof(Options)) as Options ?? new Options();

    internal readonly Runner Runner = new();
    internal ErrorListProvider? ErrorList;

    private readonly ConcurrentDictionary<string, CancellationTokenSource> _debounce = new();
    private bool _isReady;
    private bool _setupAttempted;

    protected override async Task InitializeAsync(CancellationToken ct, IProgress<ServiceProgressData> progress)
    {
        Instance = this;
        await JoinableTaskFactory.SwitchToMainThreadAsync(ct);

        ErrorList = new ErrorListProvider(this);

        // Register commands
        await AnalyzeCommand.InitializeAsync(this);
        await ClearCommand.InitializeAsync(this);
        await SetupCommand.InitializeAsync(this);

        // Check if already configured
        _isReady = !string.IsNullOrEmpty(Settings.PluginPath) && File.Exists(Settings.PluginPath);

        // Subscribe to document events
        await VS.Events.DocumentEvents.OpenedAsync.AddListenerAsync(OnDocumentOpened);
        await VS.Events.DocumentEvents.SavedAsync.AddListenerAsync(OnDocumentSaved);
    }

    protected override void Dispose(bool disposing)
    {
        Runner.Dispose();
        ErrorList?.Dispose();
        base.Dispose(disposing);
    }

    private async Task OnDocumentOpened(string path)
    {
        if (IsCpp(path) && !_isReady && !_setupAttempted)
        {
            _setupAttempted = true;
            await RunSetupAsync();
        }
        await AnalyzeIfCppAsync(path);
    }

    private async Task OnDocumentSaved(string path) => await AnalyzeIfCppAsync(path);

    public async Task RunSetupAsync()
    {
        var (clang, plugin) = await Setup.EnsureBinariesAsync();
        _isReady = clang != null && plugin != null;
    }

    private async Task AnalyzeIfCppAsync(string path)
    {
        if (!_isReady || !IsCpp(path)) return;

        if (_debounce.TryGetValue(path, out var existing))
            existing.Cancel();

        var cts = new CancellationTokenSource();
        _debounce[path] = cts;

        try
        {
            await Task.Delay(Settings.DebounceMs, cts.Token);
            _debounce.TryRemove(path, out _);
            await AnalyzeAsync(path);
        }
        catch (OperationCanceledException) { }
    }

    public async Task AnalyzeAsync(string filePath)
    {
        var config = new Runner.Config(
            Settings.ClangPath,
            Settings.PluginPath,
            Settings.Standard,
            Settings.Mode
        );

        if (string.IsNullOrEmpty(config.PluginPath) || string.IsNullOrEmpty(config.ClangPath))
        {
            return; // Not configured yet
        }

        var result = await Runner.RunAsync(filePath, config);
        var diags = Parser.Parse(result.Output, Path.GetFileName(filePath));

        await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
        UpdateErrorList(filePath, diags);
    }

    private void UpdateErrorList(string filePath, System.Collections.Generic.IEnumerable<Parser.Diagnostic> diags)
    {
        ThreadHelper.ThrowIfNotOnUIThread();
        if (ErrorList == null) return;

        for (int i = ErrorList.Tasks.Count - 1; i >= 0; i--)
        {
            if (ErrorList.Tasks[i] is ErrorTask t && t.Document == filePath)
                ErrorList.Tasks.RemoveAt(i);
        }

        foreach (var d in diags)
        {
            var task = new ErrorTask
            {
                Document = d.File,
                Line = d.Line - 1,
                Column = d.Column - 1,
                Text = d.Message,
                ErrorCategory = d.Severity switch
                {
                    Parser.Severity.Error => TaskErrorCategory.Error,
                    Parser.Severity.Warning => TaskErrorCategory.Warning,
                    _ => TaskErrorCategory.Message
                },
                Category = TaskCategory.BuildCompile
            };
            task.Navigate += (s, _) => { if (s is ErrorTask t) ErrorList.Navigate(t, Guid.Empty); };
            ErrorList.Tasks.Add(task);
        }

        ErrorList.Show();
    }

    private static bool IsCpp(string path) =>
        path.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase) ||
        path.EndsWith(".cc", StringComparison.OrdinalIgnoreCase) ||
        path.EndsWith(".cxx", StringComparison.OrdinalIgnoreCase) ||
        path.EndsWith(".c", StringComparison.OrdinalIgnoreCase) ||
        path.EndsWith(".h", StringComparison.OrdinalIgnoreCase) ||
        path.EndsWith(".hpp", StringComparison.OrdinalIgnoreCase);
}
