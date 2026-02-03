/**
 * Binary Setup - Detects or downloads clang and DTT plugin
 */

import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import * as https from 'https';
import * as os from 'os';
import * as crypto from 'crypto';
import { execSync, execFileSync } from 'child_process';

export interface Binaries {
  clangPath: string;
  pluginPath: string;
}

const LLVM_VERSION = '18.1.8';
const DTT_VERSION = 'latest';

/** Expected SHA256 checksums for DTT plugin binaries - updated with each release */
const PLUGIN_CHECKSUMS: Record<string, string> = {
  'DTTPlugin-win64.dll': '',     // Will be fetched from checksums.txt
  'libDTTPlugin-darwin.dylib': '',
  'libDTTPlugin-linux.so': '',
};

/** Compute SHA256 hash of a file */
function computeFileHash(filePath: string): string {
  const hash = crypto.createHash('sha256');
  const data = fs.readFileSync(filePath);
  hash.update(data);
  return hash.digest('hex');
}

/** Fetch checksums from release */
async function fetchChecksums(): Promise<Record<string, string>> {
  const checksumUrl = `https://github.com/adishavit/dtt/releases/${DTT_VERSION}/download/checksums.txt`;

  return new Promise((resolve) => {
    const checksums: Record<string, string> = {};

    const doRequest = (url: string, redirectCount = 0) => {
      if (redirectCount > 10) {
        resolve(checksums);
        return;
      }

      https.get(url, { headers: { 'User-Agent': 'DTT-VSCode' } }, (response) => {
        if (response.statusCode === 302 || response.statusCode === 301) {
          const location = response.headers.location;
          if (location) {
            doRequest(location, redirectCount + 1);
          } else {
            resolve(checksums);
          }
          return;
        }

        if (response.statusCode !== 200) {
          resolve(checksums);
          return;
        }

        let data = '';
        response.on('data', chunk => { data += chunk; });
        response.on('end', () => {
          // Parse checksums.txt format: "hash  filename"
          for (const line of data.split('\n')) {
            const match = line.match(/^([a-f0-9]{64})\s+(.+)$/);
            if (match) {
              checksums[match[2].trim()] = match[1];
            }
          }
          resolve(checksums);
        });
      }).on('error', () => {
        resolve(checksums);
      });
    };

    doRequest(checksumUrl);
  });
}

/** Verify file integrity against expected checksum */
async function verifyChecksum(filePath: string, expectedHash: string, fileName: string): Promise<boolean> {
  if (!expectedHash) {
    // No checksum available - warn but allow (for initial releases without checksums)
    vscode.window.showWarningMessage(`DTT: No checksum available for ${fileName}. Skipping verification.`);
    return true;
  }

  const actualHash = computeFileHash(filePath);
  if (actualHash !== expectedHash) {
    vscode.window.showErrorMessage(
      `DTT: Checksum verification failed for ${fileName}.\n` +
      `Expected: ${expectedHash}\n` +
      `Got: ${actualHash}\n` +
      `The file may be corrupted or tampered with.`
    );
    fs.unlinkSync(filePath);
    return false;
  }

  return true;
}

/** Get platform-specific info */
function getPlatform(): { os: string; arch: string; ext: string } {
  const platform = os.platform();
  const arch = os.arch();

  if (platform === 'win32') return { os: 'win64', arch: 'x64', ext: '.exe' };
  if (platform === 'darwin') return { os: 'apple-darwin', arch: arch === 'arm64' ? 'arm64' : 'x86_64', ext: '' };
  return { os: 'linux-gnu-ubuntu-22.04', arch: 'x86_64', ext: '' };
}

/** Storage directory for downloaded binaries - uses ~/.dtt/bin for cross-editor consistency */
function getStorageDir(_context: vscode.ExtensionContext): string {
  return path.join(os.homedir(), '.dtt', 'bin');
}

/** Try to find clang in common locations */
export function findClang(): string | null {
  const platform = os.platform();

  // Check PATH first
  try {
    const cmd = platform === 'win32' ? 'where clang++' : 'which clang++';
    const result = execSync(cmd, { encoding: 'utf8', stdio: ['pipe', 'pipe', 'pipe'] }).trim();
    if (result && fs.existsSync(result.split('\n')[0])) {
      return result.split('\n')[0];
    }
  } catch { /* not in PATH */ }

  // Check common install locations (including ~/.dtt/bin/llvm from auto-download)
  const dttLlvm = path.join(os.homedir(), '.dtt', 'bin', 'llvm', 'bin', 'clang++' + (platform === 'win32' ? '.exe' : ''));
  const locations: string[] = platform === 'win32' ? [
    'C:\\Program Files\\LLVM\\bin\\clang++.exe',
    'C:\\LLVM\\bin\\clang++.exe',
    dttLlvm,
  ] : platform === 'darwin' ? [
    '/opt/homebrew/opt/llvm/bin/clang++',
    '/usr/local/opt/llvm/bin/clang++',
    '/opt/homebrew/bin/clang++',
    dttLlvm,
  ] : [
    '/usr/bin/clang++-18',
    '/usr/bin/clang++',
    '/usr/local/bin/clang++',
    dttLlvm,
  ];

  for (const loc of locations) {
    if (fs.existsSync(loc)) return loc;
  }

  return null;
}

/** Try to find DTT plugin */
export function findPlugin(context: vscode.ExtensionContext): string | null {
  const { ext } = getPlatformPluginInfo();
  const platform = os.platform();
  const pluginName = platform === 'win32' ? 'DTTPlugin' : 'libDTTPlugin';

  // Check storage dir
  const storagePath = path.join(getStorageDir(context), `${pluginName}${ext}`);
  if (fs.existsSync(storagePath)) return storagePath;

  // Check config
  const configPath = vscode.workspace.getConfiguration('dtt').get<string>('pluginPath');
  if (configPath && fs.existsSync(configPath)) return configPath;

  return null;
}

function getPlatformPluginInfo(): { ext: string; asset: string } {
  const platform = os.platform();
  if (platform === 'win32') return { ext: '.dll', asset: 'DTTPlugin-win64.dll' };
  if (platform === 'darwin') return { ext: '.dylib', asset: 'libDTTPlugin-darwin.dylib' };
  return { ext: '.so', asset: 'libDTTPlugin-linux.so' };
}

/** Download file with progress */
async function downloadFile(url: string, dest: string, progress: vscode.Progress<{ message?: string; increment?: number }>): Promise<void> {
  return new Promise((resolve, reject) => {
    const file = fs.createWriteStream(dest);

    const doRequest = (url: string, redirectCount = 0) => {
      if (redirectCount > 10) {
        file.close();
        reject(new Error('Too many redirects'));
        return;
      }

      https.get(url, { headers: { 'User-Agent': 'DTT-VSCode' } }, (response) => {
        if (response.statusCode === 302 || response.statusCode === 301) {
          const location = response.headers.location;
          if (!location) {
            file.close();
            reject(new Error('Redirect with no location header'));
            return;
          }
          doRequest(location, redirectCount + 1);
          return;
        }

        if (response.statusCode !== 200) {
          file.close();
          reject(new Error(`Download failed: ${response.statusCode}`));
          return;
        }

        const totalBytes = parseInt(response.headers['content-length'] || '0', 10);
        let downloadedBytes = 0;

        response.on('data', (chunk) => {
          downloadedBytes += chunk.length;
          if (totalBytes > 0) {
            const pct = Math.round((downloadedBytes / totalBytes) * 100);
            progress.report({ message: `${pct}%` });
          }
        });

        response.pipe(file);
        file.on('finish', () => {
          file.close();
          resolve();
        });
      }).on('error', (err) => {
        file.close();
        reject(err);
      });
    };

    doRequest(url);
  });
}

/** Extract archive - uses safe argument passing to prevent injection */
async function extract(archive: string, dest: string): Promise<void> {
  const platform = os.platform();

  fs.mkdirSync(dest, { recursive: true });

  if (platform === 'win32') {
    // Use PowerShell with -LiteralPath to safely handle paths with special characters
    // Pass the script block and paths as separate arguments to avoid injection
    execFileSync('powershell', [
      '-NoProfile',
      '-NonInteractive',
      '-ExecutionPolicy', 'Bypass',
      '-Command',
      'Expand-Archive -LiteralPath $args[0] -DestinationPath $args[1] -Force',
      '-args',
      archive,
      dest
    ], { stdio: 'pipe' });
  } else {
    // Use tar for Unix - execFileSync with array args is safe
    execFileSync('tar', ['-xf', archive, '-C', dest, '--strip-components=1', '--no-same-owner'], { stdio: 'pipe' });
  }
}

/** Download and install LLVM */
export async function downloadLLVM(context: vscode.ExtensionContext): Promise<string> {
  const { os: platformOs, arch } = getPlatform();
  const storageDir = getStorageDir(context);
  fs.mkdirSync(storageDir, { recursive: true });

  // Construct download URL
  let filename: string;
  let ext: string;

  if (os.platform() === 'win32') {
    filename = `LLVM-${LLVM_VERSION}-win64`;
    ext = '.zip';  // Use zip instead of exe installer
  } else if (os.platform() === 'darwin') {
    filename = `clang+llvm-${LLVM_VERSION}-${arch}-apple-darwin`;
    ext = '.tar.xz';
  } else {
    filename = `clang+llvm-${LLVM_VERSION}-${arch}-${platformOs}`;
    ext = '.tar.xz';
  }

  const url = `https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${filename}${ext}`;
  const archivePath = path.join(storageDir, `llvm${ext}`);
  const llvmDir = path.join(storageDir, 'llvm');

  await vscode.window.withProgress({
    location: vscode.ProgressLocation.Notification,
    title: 'DTT: Downloading LLVM...',
    cancellable: false
  }, async (progress) => {
    progress.report({ message: 'Starting download...' });
    await downloadFile(url, archivePath, progress);

    progress.report({ message: 'Extracting...' });
    await extract(archivePath, llvmDir);

    // Clean up archive
    fs.unlinkSync(archivePath);
  });

  const clangPath = os.platform() === 'win32'
    ? path.join(llvmDir, 'bin', 'clang++.exe')
    : path.join(llvmDir, 'bin', 'clang++');

  // Make executable on Unix
  if (os.platform() !== 'win32') {
    fs.chmodSync(clangPath, 0o755);
  }

  return clangPath;
}

/** Download DTT plugin from releases with checksum verification */
export async function downloadPlugin(context: vscode.ExtensionContext): Promise<string> {
  const { ext, asset } = getPlatformPluginInfo();
  const storageDir = getStorageDir(context);
  fs.mkdirSync(storageDir, { recursive: true });

  const pluginName = os.platform() === 'win32' ? 'DTTPlugin' : 'libDTTPlugin';
  const pluginPath = path.join(storageDir, `${pluginName}${ext}`);

  // Get latest release URL
  const releaseUrl = `https://github.com/adishavit/dtt/releases/${DTT_VERSION}/download/${asset}`;

  try {
    // Fetch checksums first
    const checksums = await fetchChecksums();
    const expectedHash = checksums[asset] || PLUGIN_CHECKSUMS[asset];

    await vscode.window.withProgress({
      location: vscode.ProgressLocation.Notification,
      title: 'DTT: Downloading plugin...',
      cancellable: false
    }, async (progress) => {
      await downloadFile(releaseUrl, pluginPath, progress);
    });

    // Verify checksum
    if (!await verifyChecksum(pluginPath, expectedHash, asset)) {
      throw new Error('Checksum verification failed');
    }

    // Make executable on Unix
    if (os.platform() !== 'win32') {
      fs.chmodSync(pluginPath, 0o755);
    }

    return pluginPath;
  } catch (err) {
    // macOS binaries not available in releases - guide user to build from source
    if (os.platform() === 'darwin') {
      const choice = await vscode.window.showErrorMessage(
        'DTT plugin binary not available for macOS. Build from source?',
        'Show Instructions', 'Cancel'
      );
      if (choice === 'Show Instructions') {
        vscode.env.openExternal(vscode.Uri.parse(
          'https://github.com/adishavit/dtt#macos-build-from-source'
        ));
      }
      throw new Error('macOS binary not available - build from source required');
    }
    throw err;
  }
}

/** Ensure all binaries are available, downloading if needed */
export async function ensureBinaries(context: vscode.ExtensionContext): Promise<Binaries | null> {
  // Try to find existing clang
  let clangPath = findClang();

  if (!clangPath) {
    const choice = await vscode.window.showInformationMessage(
      'DTT requires LLVM/Clang. Download now?',
      'Download', 'Configure Manually'
    );

    if (choice === 'Download') {
      try {
        clangPath = await downloadLLVM(context);
      } catch (err) {
        vscode.window.showErrorMessage(`Failed to download LLVM: ${err}`);
        return null;
      }
    } else {
      vscode.commands.executeCommand('workbench.action.openSettings', 'dtt.clangPath');
      return null;
    }
  }

  // Try to find DTT plugin
  let pluginPath = findPlugin(context);

  if (!pluginPath) {
    try {
      pluginPath = await downloadPlugin(context);
    } catch (err) {
      vscode.window.showErrorMessage(
        `Failed to download DTT plugin: ${err}. ` +
        'You may need to build it manually from source.'
      );
      return null;
    }
  }

  // Save paths to config
  const config = vscode.workspace.getConfiguration('dtt');
  if (!config.get('clangPath') || config.get('clangPath') === 'clang++') {
    await config.update('clangPath', clangPath, vscode.ConfigurationTarget.Global);
  }
  if (!config.get('pluginPath')) {
    await config.update('pluginPath', pluginPath, vscode.ConfigurationTarget.Global);
  }

  vscode.window.showInformationMessage('DTT is ready!');
  return { clangPath, pluginPath };
}
