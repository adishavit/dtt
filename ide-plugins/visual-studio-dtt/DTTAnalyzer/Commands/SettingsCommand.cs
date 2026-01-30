using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;

namespace DTTAnalyzer.Commands
{
    /// <summary>
    /// Command to open DTT settings
    /// </summary>
    [Command(PackageIds.SettingsCommand)]
    internal sealed class SettingsCommand : BaseCommand<SettingsCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await VS.Settings.OpenAsync("DTT");
        }
    }
}
