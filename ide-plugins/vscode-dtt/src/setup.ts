/**
 * Binary Setup - Detects or downloads clang and DTT plugin
 */

import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import * as https from 'https';
import * as os from 'os';
import * as crypto from 'crypto';
import { IncomingMessage } from 'http';
import { pipeline } from 'stream/promises';
import { execSync, execFileSync } from 'child_process';
import { Runner } from './runner';

export interface Binaries {
  clangPath: string;
  pluginPath: string;
}

const LLVM_VERSION = '18.1.8';
const DTT_VERSION = 'latest';

/** Compute SHA256 hash of a file */
function computeFileHash(filePath: string): string {
  const hash = crypto.createHash('sha256');
  const data = fs.readFileSync(filePath);
  hash.update(data);
  return hash.digest('hex');
}

/** Open a successful response, following redirects without leaving streams open. */
async function getResponse(url: string, redirects = 0): Promise<IncomingMessage> {
  if (redirects > 10) throw new Error('Too many redirects');
  return new Promise((resolve, reject) => {
    const request = https.get(url, { headers: { 'User-Agent': 'DTT-VSCode' } }, response => {
      if ([301, 302, 303, 307, 308].includes(response.statusCode || 0)) {
        response.resume();
        const location = response.headers.location;
        if (!location) reject(new Error('Redirect with no location header'));
        else {
          try { resolve(getResponse(new URL(location, url).href, redirects + 1)); }
          catch (err) { reject(err); }
        }
      } else if (response.statusCode !== 200) {
        response.resume();
        reject(new Error(`Download failed: ${response.statusCode}`));
      } else resolve(response);
    });
    request.setTimeout(30000, () => request.destroy(new Error('Download timed out')));
    request.on('error', reject);
  });
}

/** Fetch checksums from release. Missing or interrupted responses fail setup. */
async function fetchChecksums(): Promise<Record<string, string>> {
  const url = `https://github.com/adishavit/dtt/releases/${DTT_VERSION}/download/checksums.txt`;
  const response = await getResponse(url);
  let data = '';
  for await (const chunk of response) data += chunk.toString();
  const checksums: Record<string, string> = {};
  for (const line of data.split('\n')) {
    const match = line.match(/^([a-f0-9]{64})\s+(.+)$/);
    if (match) checksums[match[2].trim()] = match[1];
  }
  return checksums;
}

/** Verify file integrity against expected checksum */
async function verifyChecksum(filePath: string, expectedHash: string, fileName: string): Promise<boolean> {
  if (!expectedHash) {
    throw new Error(`No checksum available for ${fileName}`);
  }

  const actualHash = computeFileHash(filePath);
  if (actualHash !== expectedHash) {
    vscode.window.showErrorMessage(
      `DTT: Checksum verification failed for ${fileName}.\n` +
      `Expected: ${expectedHash}\n` +
      `Got: ${actualHash}\n` +
      `The file may be corrupted or tampered with.`
    );
    return false;
  }

  return true;
}

/** Published LLVM 18.1.8 binary archives (not every OS/architecture is available). */
function llvmArchive(): string {
  const target = `${os.platform()}/${os.arch()}`;
  const suffixes: Record<string, string> = {
    'linux/x64': 'x86_64-linux-gnu-ubuntu-18.04',
    'linux/arm64': 'aarch64-linux-gnu',
    'darwin/arm64': 'arm64-apple-macos11',
    'win32/x64': 'x86_64-pc-windows-msvc'
  };
  const suffix = suffixes[target];
  if (!suffix) {
    throw new Error(`No supported LLVM ${LLVM_VERSION} archive for ${target}. ` +
      'Install a matching Clang 18 toolchain (macOS: brew install llvm@18), or build from source.');
  }
  return `clang+llvm-${LLVM_VERSION}-${suffix}.tar.xz`;
}

/** Storage directory for downloaded binaries - uses ~/.dtt/bin for cross-editor consistency */
function getStorageDir(_context: vscode.ExtensionContext): string {
  return path.join(os.homedir(), '.dtt', 'bin');
}

/** Try to find clang in common locations */
export function findClang(): string | null {
  const platform = os.platform();

  const compatible = (candidate: string): boolean => {
    try {
      const version = execFileSync(candidate, ['--version'], {
        encoding: 'utf8', timeout: 5000, stdio: ['ignore', 'pipe', 'pipe']
      });
      return /\bclang version 18(?:\.|\s)/.test(version) && !version.includes('Apple clang');
    } catch { return false; }
  };
  const configured = vscode.workspace.getConfiguration('dtt').get<string>('clangPath');
  if (configured && configured !== 'clang++') return compatible(configured) ? configured : null;

  for (const name of ['clang++-18', 'clang++']) {
    try {
      const result = execSync(`${platform === 'win32' ? 'where' : 'which'} ${name}`, {
        encoding: 'utf8', timeout: 5000, stdio: ['ignore', 'pipe', 'pipe']
      });
      for (const candidate of result.trim().split(/\r?\n/)) {
        if (candidate && compatible(candidate)) return candidate;
      }
    } catch { /* try common install locations */ }
  }

  // Check common install locations (including ~/.dtt/bin/llvm from auto-download)
  const dttLlvm = path.join(os.homedir(), '.dtt', 'bin', 'llvm', 'bin', 'clang++' + (platform === 'win32' ? '.exe' : ''));
  const locations: string[] = platform === 'win32' ? [
    'C:\\Program Files\\LLVM\\bin\\clang++.exe',
    'C:\\LLVM\\bin\\clang++.exe',
    dttLlvm,
  ] : platform === 'darwin' ? [
    '/opt/homebrew/opt/llvm@18/bin/clang++',
    '/usr/local/opt/llvm@18/bin/clang++',
    '/opt/homebrew/bin/clang++',
    dttLlvm,
  ] : [
    '/usr/bin/clang++-18',
    '/usr/bin/clang++',
    '/usr/local/bin/clang++',
    dttLlvm,
  ];

  for (const loc of locations) {
    if (fs.existsSync(loc) && compatible(loc)) return loc;
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

/** Download into the caller's staging path and propagate stream failures. */
async function downloadFile(url: string, dest: string, progress: vscode.Progress<{ message?: string; increment?: number }>): Promise<void> {
  const response = await getResponse(url);
  const totalBytes = Number(response.headers['content-length'] || 0);
  let downloadedBytes = 0;
  response.on('data', (chunk: Buffer) => {
    downloadedBytes += chunk.length;
    if (totalBytes > 0) progress.report({ message: `${Math.round(downloadedBytes / totalBytes * 100)}%` });
  });
  try {
    await pipeline(response, fs.createWriteStream(dest));
    if (totalBytes > 0 && downloadedBytes !== totalBytes) throw new Error('Incomplete download');
  } catch (err) {
    fs.rmSync(dest, { force: true });
    throw err;
  }
}

/** All supported LLVM archives are tar.xz with one top-level directory. */
async function extract(archive: string, dest: string): Promise<void> {
  fs.mkdirSync(dest, { recursive: true });
  execFileSync('tar', ['-xf', archive, '-C', dest, '--strip-components=1', '--no-same-owner'], { stdio: 'pipe' });
}

/** Download and install LLVM */
export async function downloadLLVM(context: vscode.ExtensionContext): Promise<string> {
  const filename = llvmArchive();
  const storageDir = getStorageDir(context);
  fs.mkdirSync(storageDir, { recursive: true });
  const url = `https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${filename}`;
  const archivePath = path.join(storageDir, 'llvm.tar.xz');
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

  let stagingDir: string | undefined;
  try {
    // Fetch checksums first
    const checksums = await fetchChecksums();
    const expectedHash = checksums[asset];
    if (!expectedHash) throw new Error(`No checksum available for ${asset}`);
    stagingDir = fs.mkdtempSync(path.join(storageDir, '.download-'));
    const stagedPlugin = path.join(stagingDir, asset);

    await vscode.window.withProgress({
      location: vscode.ProgressLocation.Notification,
      title: 'DTT: Downloading plugin...',
      cancellable: false
    }, async (progress) => {
      await downloadFile(releaseUrl, stagedPlugin, progress);
    });

    // Verify checksum
    if (!await verifyChecksum(stagedPlugin, expectedHash, asset)) {
      throw new Error('Checksum verification failed');
    }

    // Make executable on Unix
    if (os.platform() !== 'win32') {
      fs.chmodSync(stagedPlugin, 0o755);
    }

    fs.renameSync(stagedPlugin, pluginPath);
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
  } finally {
    if (stagingDir) fs.rmSync(stagingDir, { recursive: true, force: true });
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

  // Test the actual compiler/plugin pair before marking setup ready. A version
  // number alone cannot detect ABI mismatches or missing shared libraries.
  const storageDir = getStorageDir(context);
  fs.mkdirSync(storageDir, { recursive: true });
  const probeDir = fs.mkdtempSync(path.join(storageDir, '.probe-'));
  try {
    const source = path.join(probeDir, 'check.cpp');
    fs.writeFileSync(source, 'void check() { int *p = nullptr; *p = 1; }\n');
    const result = await new Runner().run(source, {
      clangPath, pluginPath, standard: 'c++20', mode: 'quiet'
    });
    if (result.status !== 'completed' || result.exitCode !== 1 ||
        !/error: DTT:.*null pointer/i.test(result.output)) {
      vscode.window.showErrorMessage(
        `DTT could not run with ${clangPath}. Use the plugin's matching Clang 18 build ` +
        `and install its Z3 runtime (Ubuntu: libz3-4; macOS: brew install z3).\n${result.output}`
      );
      return null;
    }
  } finally {
    fs.rmSync(probeDir, { recursive: true, force: true });
  }

  // Save paths to config only after the compiler/plugin pair has worked.
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
