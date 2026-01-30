using System.ComponentModel;
using Microsoft.VisualStudio.Shell;

namespace DTT;

/// <summary>DTT Settings - Tools > Options > DTT</summary>
public class Options : DialogPage
{
    [Category("DTT")]
    [DisplayName("Plugin Path")]
    [Description("Path to libDTTPlugin.so (WSL path if using WSL)")]
    public string PluginPath { get; set; } = "";

    [Category("DTT")]
    [DisplayName("Clang Path")]
    [Description("Path to clang++ (in WSL if using WSL)")]
    public string ClangPath { get; set; } = "clang++-18";

    [Category("DTT")]
    [DisplayName("C++ Standard")]
    [Description("C++ standard version")]
    public string Standard { get; set; } = "c++20";

    [Category("DTT")]
    [DisplayName("Mode")]
    [Description("inform = warnings for gaps; enforce = errors for all; quiet = errors for bugs only")]
    public string Mode { get; set; } = "inform";

    [Category("DTT")]
    [DisplayName("Use WSL")]
    [Description("Run clang through Windows Subsystem for Linux")]
    public bool UseWsl { get; set; } = true;

    [Category("DTT")]
    [DisplayName("Debounce (ms)")]
    [Description("Delay before analysis after typing")]
    public int DebounceMs { get; set; } = 500;
}
