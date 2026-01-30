using System;
using System.Threading;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace DTTAnalyzer.Commands
{
    /// <summary>
    /// Command to analyze all C++ files in the solution
    /// </summary>
    [Command(PackageIds.AnalyzeSolutionCommand)]
    internal sealed class AnalyzeSolutionCommand : BaseCommand<AnalyzeSolutionCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            var solution = await VS.Solutions.GetCurrentSolutionAsync();
            if (solution == null)
            {
                await VS.MessageBox.ShowWarningAsync("DTT", "No solution is currently open.");
                return;
            }

            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            // Get the status bar
            var statusBar = await VS.GetServiceAsync<SVsStatusbar, IVsStatusbar>();
            if (statusBar == null)
                return;

            uint cookie = 0;

            try
            {
                // Initialize progress bar
                statusBar.Progress(ref cookie, 1, "DTT: Analyzing solution...", 0, 100);

                var cts = new CancellationTokenSource();

                var progress = new Progress<string>(message =>
                {
                    ThreadHelper.JoinableTaskFactory.Run(async () =>
                    {
                        await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                        statusBar.SetText(message);
                    });
                });

                await DTTService.Instance.AnalyzeSolutionAsync(progress, cts.Token);

                await VS.StatusBar.ShowMessageAsync("DTT: Solution analysis complete");
            }
            catch (Exception ex)
            {
                await VS.StatusBar.ShowMessageAsync("DTT: Solution analysis failed");
                await VS.MessageBox.ShowErrorAsync("DTT Error", ex.Message);
            }
            finally
            {
                // Clear progress bar
                statusBar.Progress(ref cookie, 0, "", 0, 0);
            }
        }
    }
}
