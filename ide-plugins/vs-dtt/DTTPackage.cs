using System;
using System.Collections.Concurrent;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using EnvDTE;
using EnvDTE80;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;
using Task = System.Threading.Tasks.Task;

namespace DTT
{
    [PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
    [Guid("a1b2c3d4-e5f6-7890-abcd-ef1234567890")]
    [ProvideAutoLoad(VSConstants.UICONTEXT.SolutionExists_string, PackageAutoLoadFlags.BackgroundLoad)]
    [ProvideOptionPage(typeof(Options), "DTT", "General", 0, 0, true)]
    [ProvideMenuResource("Menus.ctmenu", 1)]
    public sealed class DTTPackage : ToolkitPackage
    {
        public static DTTPackage Instance { get; private set; }
        public Options Settings { get { return GetDialogPage(typeof(Options)) as Options ?? new Options(); } }

        internal readonly Runner Runner = new Runner();
        internal ErrorListProvider ErrorList;

        private readonly ConcurrentDictionary<string, CancellationTokenSource> _debounce =
            new ConcurrentDictionary<string, CancellationTokenSource>();
        private bool _isReady;
        private bool _setupAttempted;
        private DTE2 _dte;
        private EnvDTE.DocumentEvents _docEvents;

        protected override async Task InitializeAsync(CancellationToken ct, IProgress<ServiceProgressData> progress)
        {
            Instance = this;
            await JoinableTaskFactory.SwitchToMainThreadAsync(ct);

            ErrorList = new ErrorListProvider(this);

            await AnalyzeCommand.InitializeAsync(this);
            await ClearCommand.InitializeAsync(this);
            await SetupCommand.InitializeAsync(this);

            _isReady = false; // Setup validates the compiler/plugin pair on first use.

            _dte = await GetServiceAsync(typeof(DTE)) as DTE2;
            if (_dte != null)
            {
                _docEvents = _dte.Events.DocumentEvents;
                _docEvents.DocumentOpened += OnDocumentOpened;
                _docEvents.DocumentSaved += OnDocumentSaved;
            }
        }

        protected override void Dispose(bool disposing)
        {
            // Unregister event handlers to prevent memory leaks
            // Only access COM objects on UI thread (CheckAccess verifies at runtime)
#pragma warning disable VSTHRD010 // Invoke single-threaded types on Main thread
            if (_docEvents != null && ThreadHelper.CheckAccess())
            {
                _docEvents.DocumentOpened -= OnDocumentOpened;
                _docEvents.DocumentSaved -= OnDocumentSaved;
            }
#pragma warning restore VSTHRD010

            // Dispose all pending debounce tokens
            foreach (var kvp in _debounce)
            {
                try
                {
                    kvp.Value.Cancel();
                    kvp.Value.Dispose();
                }
                catch { }
            }
            _debounce.Clear();

            Runner.Dispose();
            if (ErrorList != null) ErrorList.Dispose();
            base.Dispose(disposing);
        }

        private void OnDocumentOpened(Document doc)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (doc == null || string.IsNullOrEmpty(doc.FullName)) return;
            var path = doc.FullName;

            JoinableTaskFactory.RunAsync(async () =>
            {
                if (IsCpp(path) && !_isReady && !_setupAttempted)
                {
                    _setupAttempted = true;
                    await RunSetupAsync();
                }
                await AnalyzeIfCppAsync(path);
            }).FileAndForget("DTT/OnDocumentOpened");
        }

        private void OnDocumentSaved(Document doc)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (doc == null || string.IsNullOrEmpty(doc.FullName)) return;
            var path = doc.FullName;

            JoinableTaskFactory.RunAsync(async () =>
            {
                await AnalyzeIfCppAsync(path);
            }).FileAndForget("DTT/OnDocumentSaved");
        }

        public async Task RunSetupAsync()
        {
            var result = await Setup.EnsureBinariesAsync();
            _isReady = result != null && result.ClangPath != null && result.PluginPath != null;
        }

        private async Task AnalyzeIfCppAsync(string path)
        {
            if (!_isReady || !IsCpp(path)) return;

            // Atomically replace existing token and dispose the old one
            var cts = new CancellationTokenSource();
            CancellationTokenSource existing = _debounce.AddOrUpdate(
                path,
                cts,
                (_, old) =>
                {
                    try
                    {
                        old.Cancel();
                        old.Dispose();
                    }
                    catch { }
                    return cts;
                });

            try
            {
                await Task.Delay(Settings.DebounceMs, cts.Token);
                CancellationTokenSource removed;
                if (_debounce.TryRemove(path, out removed))
                {
                    // Only dispose if it's still our token (not replaced by another)
                    if (ReferenceEquals(removed, cts))
                    {
                        // Don't dispose yet - we're about to use it
                    }
                }
                await AnalyzeAsync(path);
            }
            catch (OperationCanceledException) { }
            finally
            {
                // Clean up if we're the current token
                CancellationTokenSource current;
                if (_debounce.TryGetValue(path, out current) && ReferenceEquals(current, cts))
                {
                    _debounce.TryRemove(path, out _);
                }
                cts.Dispose();
            }
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
                return;
            }

            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            if (!await Runner.ProbeAsync(config))
            {
                UpdateErrorList(filePath, new[] { new Parser.Diagnostic(filePath, 1, 1, Parser.Severity.Warning,
                    "DTT compiler/plugin check failed. Use the matching Clang 18 build and Z3 runtime.") });
                return;
            }
            var view = await VS.Documents.GetDocumentViewAsync(filePath);
            var snapshot = view?.TextBuffer?.CurrentSnapshot;
            config.Content = snapshot?.GetText();
            var result = await Runner.RunAsync(filePath, config);
            var diags = Parser.Parse(result.Output, Path.GetFileName(filePath));

            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            if (snapshot != null && view.TextBuffer.CurrentSnapshot != snapshot) return;
            if (result.Output == "Cancelled") return;
            var compilerError = result.Output.Split('\n').Any(line => line.Contains("error:") && !line.Contains("DTT:"));
            if (compilerError || (result.ExitCode != 0 && !diags.Any(d => d.Severity == Parser.Severity.Error)))
                diags = new[] { new Parser.Diagnostic(filePath, 1, 1, Parser.Severity.Warning,
                    "DTT analysis failed: " + result.Output) };
            UpdateErrorList(filePath, diags);
        }

        private void UpdateErrorList(string filePath, System.Collections.Generic.IEnumerable<Parser.Diagnostic> diags)
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (ErrorList == null) return;

            for (int i = ErrorList.Tasks.Count - 1; i >= 0; i--)
            {
                var t = ErrorList.Tasks[i] as ErrorTask;
                if (t != null && t.Document == filePath)
                    ErrorList.Tasks.RemoveAt(i);
            }

            foreach (var d in diags)
            {
                TaskErrorCategory category;
                switch (d.Severity)
                {
                    case Parser.Severity.Error:
                        category = TaskErrorCategory.Error;
                        break;
                    case Parser.Severity.Warning:
                        category = TaskErrorCategory.Warning;
                        break;
                    default:
                        category = TaskErrorCategory.Message;
                        break;
                }

                var task = new ErrorTask
                {
                    Document = d.File,
                    Line = d.Line - 1,
                    Column = d.Column - 1,
                    Text = d.Message,
                    ErrorCategory = category,
                    Category = TaskCategory.BuildCompile
                };
                task.Navigate += (s, e) =>
                {
                    var et = s as ErrorTask;
                    if (et != null) ErrorList.Navigate(et, Guid.Empty);
                };
                ErrorList.Tasks.Add(task);
            }

            ErrorList.Show();
        }

        private static bool IsCpp(string path)
        {
            return path.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase) ||
                   path.EndsWith(".cc", StringComparison.OrdinalIgnoreCase) ||
                   path.EndsWith(".cxx", StringComparison.OrdinalIgnoreCase) ||
                   path.EndsWith(".c", StringComparison.OrdinalIgnoreCase) ||
                   path.EndsWith(".h", StringComparison.OrdinalIgnoreCase) ||
                   path.EndsWith(".hpp", StringComparison.OrdinalIgnoreCase);
        }
    }
}
