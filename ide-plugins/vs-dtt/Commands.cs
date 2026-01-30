using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;

namespace DTT;

[Command(PackageIds.AnalyzeCommand)]
internal sealed class AnalyzeCommand : BaseCommand<AnalyzeCommand>
{
    protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
    {
        var doc = await VS.Documents.GetActiveDocumentViewAsync();
        if (doc?.FilePath != null && DTTPackage.Instance != null)
            await DTTPackage.Instance.AnalyzeAsync(doc.FilePath);
    }
}

[Command(PackageIds.ClearCommand)]
internal sealed class ClearCommand : BaseCommand<ClearCommand>
{
    protected override Task ExecuteAsync(OleMenuCmdEventArgs e)
    {
        ThreadHelper.ThrowIfNotOnUIThread();
        DTTPackage.Instance?.ErrorList?.Tasks.Clear();
        return Task.CompletedTask;
    }
}

internal static class PackageIds
{
    public const int AnalyzeCommand = 0x0100;
    public const int ClearCommand = 0x0101;
}
