using System.ComponentModel;
using Community.VisualStudio.Toolkit;

namespace DTTAnalyzer
{
    /// <summary>
    /// DTT configuration options
    /// </summary>
    internal partial class OptionsProvider
    {
        [ComVisible(true)]
        public class GeneralOptions : BaseOptionPage<DTTAnalyzer.GeneralOptions> { }
    }

    /// <summary>
    /// DTT general settings
    /// </summary>
    public class GeneralOptions : BaseOptionModel<GeneralOptions>
    {
        [Category("DTT Plugin")]
        [DisplayName("Plugin Path")]
        [Description("Path to the DTT plugin shared library (libDTTPlugin.so or DTTPlugin.dll). For Windows with WSL, this should be the WSL path.")]
        [DefaultValue("")]
        public string PluginPath { get; set; } = "";

        [Category("DTT Plugin")]
        [DisplayName("Clang Path")]
        [Description("Path to the clang++ compiler. For Windows with WSL, use 'wsl clang++-18' or similar.")]
        [DefaultValue("clang++")]
        public string ClangPath { get; set; } = "clang++";

        [Category("Analysis")]
        [DisplayName("Analysis Mode")]
        [Description("DTT analysis mode: Inform (default), Enforce (strict), or Quiet (permissive)")]
        [DefaultValue(DTTMode.Inform)]
        [TypeConverter(typeof(EnumConverter))]
        public DTTMode Mode { get; set; } = DTTMode.Inform;

        [Category("Analysis")]
        [DisplayName("C++ Standard")]
        [Description("C++ standard version for analysis")]
        [DefaultValue(CppStandard.Cpp20)]
        [TypeConverter(typeof(EnumConverter))]
        public CppStandard Standard { get; set; } = CppStandard.Cpp20;

        [Category("Analysis")]
        [DisplayName("Analyze on Save")]
        [Description("Automatically run DTT analysis when saving C++ files")]
        [DefaultValue(true)]
        public bool AnalyzeOnSave { get; set; } = true;

        [Category("Analysis")]
        [DisplayName("Analyze on Build")]
        [Description("Run DTT analysis as part of the build process")]
        [DefaultValue(false)]
        public bool AnalyzeOnBuild { get; set; } = false;

        [Category("Analysis")]
        [DisplayName("Additional Include Paths")]
        [Description("Additional include paths for analysis (semicolon-separated)")]
        [DefaultValue("")]
        public string IncludePaths { get; set; } = "";

        [Category("Analysis")]
        [DisplayName("Additional Compiler Flags")]
        [Description("Additional compiler flags to pass to clang++ (space-separated)")]
        [DefaultValue("")]
        public string AdditionalFlags { get; set; } = "";

        [Category("WSL")]
        [DisplayName("Use WSL")]
        [Description("Use Windows Subsystem for Linux to run clang++ (required on Windows)")]
        [DefaultValue(true)]
        public bool UseWsl { get; set; } = true;

        [Category("WSL")]
        [DisplayName("WSL Distribution")]
        [Description("WSL distribution to use (leave empty for default)")]
        [DefaultValue("")]
        public string WslDistribution { get; set; } = "";
    }

    /// <summary>
    /// DTT analysis modes
    /// </summary>
    public enum DTTMode
    {
        [Description("Inform - Warnings for gaps, errors for bugs (default)")]
        Inform,

        [Description("Enforce - Errors for everything including gaps")]
        Enforce,

        [Description("Quiet - Errors only for proven bugs")]
        Quiet
    }

    /// <summary>
    /// C++ standard versions
    /// </summary>
    public enum CppStandard
    {
        [Description("C++17")]
        Cpp17,

        [Description("C++20")]
        Cpp20,

        [Description("C++23")]
        Cpp23
    }
}
