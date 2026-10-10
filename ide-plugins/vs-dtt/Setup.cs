using System;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Collections.Generic;
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
        private const string DttVersion = "latest";
        private static readonly HttpClient Http = new HttpClient();

        /// <summary>Compute SHA256 hash of a file</summary>
        private static string ComputeFileHash(string filePath)
        {
            using (var sha256 = SHA256.Create())
            using (var stream = File.OpenRead(filePath))
            {
                var hash = sha256.ComputeHash(stream);
                var sb = new StringBuilder();
                foreach (var b in hash)
                    sb.Append(b.ToString("x2"));
                return sb.ToString();
            }
        }

        /// <summary>Fetch checksums from release</summary>
        private static async Task<Dictionary<string, string>> FetchChecksumsAsync()
        {
            var checksums = new Dictionary<string, string>();
            var checksumUrl = string.Format("https://github.com/adishavit/dtt/releases/{0}/download/checksums.txt", DttVersion);

            try
            {
                var content = await Http.GetStringAsync(checksumUrl);
                foreach (var line in content.Split(new[] { '\n', '\r' }, StringSplitOptions.RemoveEmptyEntries))
                {
                    // Format: "hash  filename"
                    var parts = line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
                    if (parts.Length >= 2 && parts[0].Length == 64)
                    {
                        checksums[parts[parts.Length - 1]] = parts[0];
                    }
                }
            }
            catch
            {
                // Checksums file not available - continue without verification
            }

            return checksums;
        }

        /// <summary>Verify file integrity</summary>
        private static async Task<bool> VerifyChecksumAsync(string filePath, string expectedHash, string fileName)
        {
            if (string.IsNullOrEmpty(expectedHash))
            {
                await VS.StatusBar.ShowMessageAsync("DTT: Warning - No checksum available for " + fileName);
                return true; // Allow for releases without checksums
            }

            var actualHash = ComputeFileHash(filePath);
            if (!actualHash.Equals(expectedHash, StringComparison.OrdinalIgnoreCase))
            {
                File.Delete(filePath);
                await VS.MessageBox.ShowErrorAsync("DTT",
                    string.Format("Checksum verification failed for {0}.\n" +
                    "Expected: {1}\n" +
                    "Got: {2}\n" +
                    "The file may be corrupted or tampered with.",
                    fileName, expectedHash, actualHash));
                return false;
            }

            return true;
        }

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

            var filename = string.Format("clang+llvm-{0}-x86_64-pc-windows-msvc.tar.xz", LlvmVersion);
            var url = string.Format("https://github.com/llvm/llvm-project/releases/download/llvmorg-{0}/{1}", LlvmVersion, filename);
            var archivePath = Path.Combine(StorageDir, filename);
            var llvmDir = Path.Combine(StorageDir, "llvm");

            await VS.StatusBar.ShowMessageAsync("DTT: Downloading LLVM...");

            using (var response = await Http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead))
            {
                response.EnsureSuccessStatusCode();
                using (var fs = new FileStream(archivePath, FileMode.Create))
                {
                    await response.Content.CopyToAsync(fs);
                }
            }

            await VS.StatusBar.ShowMessageAsync("DTT: Extracting LLVM...");

            Directory.CreateDirectory(llvmDir);
            await Task.Run(() =>
            {
                using (var process = Process.Start(new ProcessStartInfo("tar.exe")
                {
                    Arguments = "-xf \"" + archivePath + "\" -C \"" + llvmDir + "\" --strip-components=1 --no-same-owner",
                    UseShellExecute = false,
                    CreateNoWindow = true
                }))
                {
                    process.WaitForExit();
                    if (process.ExitCode != 0)
                        throw new IOException("LLVM extraction failed with code " + process.ExitCode);
                }
            });
            File.Delete(archivePath);

            await VS.StatusBar.ClearAsync();
            return Path.Combine(llvmDir, "bin", "clang++.exe");
        }

        /// <summary>Download DTT plugin from releases with checksum verification</summary>
        public static async Task<string> DownloadPluginAsync()
        {
            Directory.CreateDirectory(StorageDir);

            var pluginPath = Path.Combine(StorageDir, "DTTPlugin.dll");
            var asset = "DTTPlugin-win64.dll";
            var url = string.Format("https://github.com/adishavit/dtt/releases/{0}/download/{1}", DttVersion, asset);

            // Fetch checksums first
            var checksums = await FetchChecksumsAsync();
            string expectedHash;
            checksums.TryGetValue(asset, out expectedHash);

            await VS.StatusBar.ShowMessageAsync("DTT: Downloading plugin...");

            using (var response = await Http.GetAsync(url))
            {
                response.EnsureSuccessStatusCode();
                using (var fs = new FileStream(pluginPath, FileMode.Create))
                {
                    await response.Content.CopyToAsync(fs);
                }
            }

            // Verify checksum
            if (!await VerifyChecksumAsync(pluginPath, expectedHash, asset))
            {
                throw new InvalidOperationException("Checksum verification failed");
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
