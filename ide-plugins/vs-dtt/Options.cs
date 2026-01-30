using System.ComponentModel;
using Microsoft.VisualStudio.Shell;

namespace DTT;

/// <summary>DTT Settings - Tools > Options > DTT</summary>
public class Options : DialogPage
{
    [Category("DTT")]
    [DisplayName("Plugin Path")]
    [Description("Path to DTTPlugin.dll (e.g., C:\\LLVM\\lib\\DTTPlugin.dll)")]
    public string PluginPath { get; set; } = "";

    [Category("DTT")]
    [DisplayName("Clang Path")]
    [Description("Path to clang++.exe (e.g., C:\\LLVM\\bin\\clang++.exe)")]
    public string ClangPath { get; set; } = "clang++";

    [Category("DTT")]
    [DisplayName("C++ Standard")]
    [Description("C++ standard version")]
    public string Standard { get; set; } = "c++20";

    [Category("DTT")]
    [DisplayName("Mode")]
    [Description("inform = warnings for gaps; enforce = errors for all; quiet = errors for bugs only")]
    public string Mode { get; set; } = "inform";

    [Category("DTT")]
    [DisplayName("Debounce (ms)")]
    [Description("Delay before analysis after typing")]
    public int DebounceMs { get; set; } = 500;
}
