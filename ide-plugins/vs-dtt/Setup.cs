using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Net.Http;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;

namespace DTT
{
    /// <summary>
    /// Binary Setup - Detects or downloads clang and DTT plugin
    /// </summary>
    public static class Setup
    {
        private const string LlvmVersion = "18.1.8";
        private static readonly HttpClient Http = new HttpClient();

        public class BinaryPaths
        {
            public string ClangPath { get; set; }
            public string PluginPath { get; set; }
        }

        private static string StorageDir
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DTT", "bin"); }
        }

        /// <summary>Find clang in common Windows locations</summary>
        public static string FindClang()
        {
            // Check PATH
            try
            {
                var psi = new ProcessStartInfo("where", "clang++")
                {
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    CreateNoWindow = true
                };
                using (var proc = Process.Start(psi))
                {
                    var output = proc != null ? proc.StandardOutput.ReadToEnd().Trim() : null;
                    if (proc != null) proc.WaitForExit();
                    if (!string.IsNullOrEmpty(output) && File.Exists(output.Split('\n')[0]))
                        return output.Split('\n')[0].Trim();
                }
            }
            catch { }

            // Check common locations
            string[] locations = {
                @"C:\Program Files\LLVM\bin\clang++.exe",
                @"C:\LLVM\bin\clang++.exe",
                Path.Combine(StorageDir, "llvm", "bin", "clang++.exe")
            };

            foreach (var loc in locations)
                if (File.Exists(loc)) return loc;

            return null;
        }

        /// <summary>Find DTT plugin</summary>
        public static string FindPlugin()
        {
            var storagePath = Path.Combine(StorageDir, "DTTPlugin.dll");
            if (File.Exists(storagePath)) return storagePath;

            // Check configured path
            var configPath = DTTPackage.Instance != null ? DTTPackage.Instance.Settings.PluginPath : null;
            if (!string.IsNullOrEmpty(configPath) && File.Exists(configPath))
                return configPath;

            return null;
        }

        /// <summary>Download LLVM from official releases</summary>
        public static async Task<string> DownloadLlvmAsync()
        {
            Directory.CreateDirectory(StorageDir);

            var filename = string.Format("LLVM-{0}-win64.zip", LlvmVersion);
            var url = string.Format("https://github.com/llvm/llvm-project/releases/download/llvmorg-{0}/{1}", LlvmVersion, filename);
            var zipPath = Path.Combine(StorageDir, filename);
            var llvmDir = Path.Combine(StorageDir, "llvm");

            await VS.StatusBar.ShowMessageAsync("DTT: Downloading LLVM...");

            using (var response = await Http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead))
            {
                response.EnsureSuccessStatusCode();
                using (var fs = new FileStream(zipPath, FileMode.Create))
                {
                    await response.Content.CopyToAsync(fs);
                }
            }

            await VS.StatusBar.ShowMessageAsync("DTT: Extracting LLVM...");

            if (Directory.Exists(llvmDir))
                Directory.Delete(llvmDir, true);

            ZipFile.ExtractToDirectory(zipPath, StorageDir);

            // LLVM zip extracts to LLVM-{version}-win64, rename to llvm
            var extractedDir = Path.Combine(StorageDir, string.Format("LLVM-{0}-win64", LlvmVersion));
            if (Directory.Exists(extractedDir))
                Directory.Move(extractedDir, llvmDir);

            File.Delete(zipPath);

            await VS.StatusBar.ClearAsync();
            return Path.Combine(llvmDir, "bin", "clang++.exe");
        }

        /// <summary>Download DTT plugin from releases</summary>
        public static async Task<string> DownloadPluginAsync()
        {
            Directory.CreateDirectory(StorageDir);

            var pluginPath = Path.Combine(StorageDir, "DTTPlugin.dll");
            var url = "https://github.com/adishavit/dtt/releases/latest/download/DTTPlugin-win64.dll";

            await VS.StatusBar.ShowMessageAsync("DTT: Downloading plugin...");

            using (var response = await Http.GetAsync(url))
            {
                response.EnsureSuccessStatusCode();
                var bytes = await response.Content.ReadAsByteArrayAsync();
                File.WriteAllBytes(pluginPath, bytes);
            }

            await VS.StatusBar.ClearAsync();
            return pluginPath;
        }

        /// <summary>Ensure binaries are available, downloading if needed</summary>
        public static async Task<BinaryPaths> EnsureBinariesAsync()
        {
            var clangPath = FindClang();
            var pluginPath = FindPlugin();

            if (clangPath == null)
            {
                var result = await VS.MessageBox.ShowAsync(
                    "DTT requires LLVM/Clang. Download now?",
                    "DTT Setup",
                    Microsoft.VisualStudio.Shell.Interop.OLEMSGICON.OLEMSGICON_QUERY,
                    Microsoft.VisualStudio.Shell.Interop.OLEMSGBUTTON.OLEMSGBUTTON_YESNO);

                if (result == Microsoft.VisualStudio.VSConstants.MessageBoxResult.IDYES)
                {
                    try
                    {
                        clangPath = await DownloadLlvmAsync();
                    }
                    catch (Exception ex)
                    {
                        await VS.MessageBox.ShowErrorAsync("DTT", "Failed to download LLVM: " + ex.Message);
                        return null;
                    }
                }
                else
                {
                    return null;
                }
            }

            if (pluginPath == null)
            {
                try
                {
                    pluginPath = await DownloadPluginAsync();
                }
                catch (Exception ex)
                {
                    await VS.MessageBox.ShowErrorAsync("DTT",
                        "Failed to download DTT plugin: " + ex.Message + "\n" +
                        "You may need to build it manually from source.");
                    return new BinaryPaths { ClangPath = clangPath, PluginPath = null };
                }
            }

            // Update settings
            if (DTTPackage.Instance != null)
            {
                var settings = DTTPackage.Instance.Settings;
                if (string.IsNullOrEmpty(settings.ClangPath) || settings.ClangPath == "clang++")
                    settings.ClangPath = clangPath;
                if (string.IsNullOrEmpty(settings.PluginPath))
                    settings.PluginPath = pluginPath;
                settings.SaveSettingsToStorage();
            }

            await VS.MessageBox.ShowAsync("DTT is ready!", "DTT Setup");
            return new BinaryPaths { ClangPath = clangPath, PluginPath = pluginPath };
        }
    }
}
