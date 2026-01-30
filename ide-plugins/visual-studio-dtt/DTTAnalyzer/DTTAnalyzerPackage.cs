using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;

namespace DTTAnalyzer
{
    /// <summary>
    /// DTT Analyzer Visual Studio Package
    /// Provides compile-time memory safety analysis for C++ using the DTT Clang plugin.
    /// </summary>
    [PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
    [InstalledProductRegistration(Vsix.Name, Vsix.Description, Vsix.Version)]
    [ProvideMenuResource("Menus.ctmenu", 1)]
    [ProvideOptionPage(typeof(OptionsProvider.GeneralOptions), "DTT", "General", 0, 0, true)]
    [ProvideProfile(typeof(OptionsProvider.GeneralOptions), "DTT", "General", 0, 0, true)]
    [ProvideAutoLoad(VSConstants.UICONTEXT.SolutionOpening_string, PackageAutoLoadFlags.BackgroundLoad)]
    [Guid(PackageGuids.DTTAnalyzerString)]
    public sealed class DTTAnalyzerPackage : ToolkitPackage
    {
        public static DTTAnalyzerPackage? Instance { get; private set; }

        protected override async Task InitializeAsync(CancellationToken cancellationToken, IProgress<ServiceProgressData> progress)
        {
            await base.InitializeAsync(cancellationToken, progress);

            Instance = this;

            // Register commands
            await this.RegisterCommandsAsync();

            // Initialize the DTT service
            await DTTService.Instance.InitializeAsync();

            // Hook into document save events for auto-analysis
            await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);
            VS.Events.DocumentEvents.Saved += OnDocumentSaved;

            await OutputWindow.WriteLineAsync("DTT Analyzer initialized");
        }

        private void OnDocumentSaved(string filePath)
        {
            var options = GeneralOptions.Instance;
            if (options.AnalyzeOnSave && IsCppFile(filePath))
            {
                _ = DTTService.Instance.AnalyzeFileAsync(filePath);
            }
        }

        internal static bool IsCppFile(string filePath)
        {
            if (string.IsNullOrEmpty(filePath))
                return false;

            var extension = System.IO.Path.GetExtension(filePath).ToLowerInvariant();
            return extension switch
            {
                ".cpp" or ".cc" or ".cxx" or ".c++" or ".c" or ".h" or ".hpp" or ".hxx" => true,
                _ => false
            };
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                VS.Events.DocumentEvents.Saved -= OnDocumentSaved;
            }
            base.Dispose(disposing);
        }
    }

    /// <summary>
    /// VSIX manifest constants
    /// </summary>
    internal static class Vsix
    {
        public const string Name = "DTT - Dependent Type Tracking";
        public const string Description = "Compile-time memory safety analysis for C++";
        public const string Version = "1.0.0";
    }

    /// <summary>
    /// Package GUIDs
    /// </summary>
    internal static class PackageGuids
    {
        public const string DTTAnalyzerString = "a1b2c3d4-e5f6-7890-abcd-ef1234567890";
        public static readonly Guid DTTAnalyzer = new(DTTAnalyzerString);
    }
}
