using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;

namespace DTTAnalyzer.Commands
{
    /// <summary>
    /// Command to analyze the current file
    /// </summary>
    [Command(PackageIds.AnalyzeFileCommand)]
    internal sealed class AnalyzeFileCommand : BaseCommand<AnalyzeFileCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            var docView = await VS.Documents.GetActiveDocumentViewAsync();
            if (docView?.FilePath == null)
            {
                await VS.MessageBox.ShowWarningAsync("DTT", "No file is currently open.");
                return;
            }

            if (!DTTAnalyzerPackage.IsCppFile(docView.FilePath))
            {
                await VS.MessageBox.ShowWarningAsync("DTT", "The current file is not a C++ file.");
                return;
            }

            await VS.StatusBar.ShowMessageAsync("DTT: Analyzing file...");

            try
            {
                var diagnostics = await DTTService.Instance.AnalyzeFileAsync(docView.FilePath);

                var errors = 0;
                var warnings = 0;
                foreach (var d in diagnostics)
                {
                    if (d.Severity == DiagnosticSeverity.Error) errors++;
                    else if (d.Severity == DiagnosticSeverity.Warning) warnings++;
                }

                await VS.StatusBar.ShowMessageAsync($"DTT: Analysis complete - {errors} error(s), {warnings} warning(s)");
            }
            catch (System.Exception ex)
            {
                await VS.StatusBar.ShowMessageAsync("DTT: Analysis failed");
                await VS.MessageBox.ShowErrorAsync("DTT Error", ex.Message);
            }
        }
    }

    /// <summary>
    /// Context menu command to analyze the current file
    /// </summary>
    [Command(PackageIds.AnalyzeFileContextCommand)]
    internal sealed class AnalyzeFileContextCommand : BaseCommand<AnalyzeFileContextCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await AnalyzeFileCommand.Instance.ExecuteAsync(e);
        }

        protected override void BeforeQueryStatus(System.EventArgs e)
        {
            ThreadHelper.JoinableTaskFactory.Run(async () =>
            {
                var docView = await VS.Documents.GetActiveDocumentViewAsync();
                Command.Visible = docView?.FilePath != null && DTTAnalyzerPackage.IsCppFile(docView.FilePath);
            });
        }
    }
}
