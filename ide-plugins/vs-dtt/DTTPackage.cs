using System;
using System.Collections.Concurrent;
using System.IO;
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

            _isReady = !string.IsNullOrEmpty(Settings.PluginPath) && File.Exists(Settings.PluginPath);

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

            CancellationTokenSource existing;
            if (_debounce.TryGetValue(path, out existing))
                existing.Cancel();

            var cts = new CancellationTokenSource();
            _debounce[path] = cts;

            try
            {
                await Task.Delay(Settings.DebounceMs, cts.Token);
                CancellationTokenSource removed;
                _debounce.TryRemove(path, out removed);
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
                return;
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
