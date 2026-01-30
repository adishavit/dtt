using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;

namespace DTTAnalyzer.Commands
{
    /// <summary>
    /// Command to clear all DTT diagnostics
    /// </summary>
    [Command(PackageIds.ClearDiagnosticsCommand)]
    internal sealed class ClearDiagnosticsCommand : BaseCommand<ClearDiagnosticsCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await DTTService.Instance.ClearDiagnosticsAsync();
            await VS.StatusBar.ShowMessageAsync("DTT: Diagnostics cleared");
        }
    }
}
