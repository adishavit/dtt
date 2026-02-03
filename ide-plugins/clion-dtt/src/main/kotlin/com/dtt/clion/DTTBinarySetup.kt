package com.dtt.clion

import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.application.ApplicationManager
import com.intellij.openapi.progress.ProgressIndicator
import com.intellij.openapi.progress.ProgressManager
import com.intellij.openapi.progress.Task
import com.intellij.openapi.project.Project
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URI

/**
 * Handles auto-download of Clang and DTT plugin binaries.
 * Storage location: ~/.dtt/bin/ for cross-editor consistency.
 */
object DTTBinarySetup {
    private const val LLVM_VERSION = "18.1.8"
    private const val DTT_VERSION = "latest"

    private val storageDir: File
        get() = File(System.getProperty("user.home"), ".dtt/bin")

    private val isWindows: Boolean
        get() = System.getProperty("os.name").lowercase().contains("win")

    private val isMacOS: Boolean
        get() = System.getProperty("os.name").lowercase().contains("mac")

    private val pluginExtension: String
        get() = when {
            isWindows -> ".dll"
            isMacOS -> ".dylib"
            else -> ".so"
        }

    private val pluginName: String
        get() = if (isWindows) "DTTPlugin" else "libDTTPlugin"

    /**
     * Find clang++ in PATH or common locations.
     */
    fun findClang(): String? {
        // Check PATH first
        listOf("clang++", "clang++-18").forEach { cmd ->
            try {
                val process = ProcessBuilder(if (isWindows) listOf("where", cmd) else listOf("which", cmd))
                    .redirectErrorStream(true)
                    .start()
                val result = process.inputStream.bufferedReader().readText().trim()
                if (process.waitFor() == 0 && result.isNotEmpty()) {
                    val path = result.lines().first()
                    if (File(path).exists()) return path
                }
            } catch (_: Exception) { }
        }

        // Check common locations
        val locations = when {
            isWindows -> listOf(
                "C:/Program Files/LLVM/bin/clang++.exe",
                "C:/LLVM/bin/clang++.exe",
                "${storageDir.absolutePath}/llvm/bin/clang++.exe"
            )
            isMacOS -> listOf(
                "/opt/homebrew/opt/llvm/bin/clang++",
                "/usr/local/opt/llvm/bin/clang++",
                "/opt/homebrew/bin/clang++",
                "${storageDir.absolutePath}/llvm/bin/clang++"
            )
            else -> listOf(
                "/usr/bin/clang++-18",
                "/usr/bin/clang++",
                "/usr/local/bin/clang++",
                "${storageDir.absolutePath}/llvm/bin/clang++"
            )
        }

        return locations.firstOrNull { File(it).exists() }
    }

    /**
     * Find DTT plugin in common locations.
     */
    fun findPlugin(): String? {
        val locations = when {
            isWindows -> listOf(
                "${storageDir.absolutePath}/$pluginName$pluginExtension",
                "${System.getenv("LOCALAPPDATA") ?: ""}/DTT/bin/$pluginName$pluginExtension",
                "C:/Program Files/DTT/bin/$pluginName$pluginExtension"
            )
            else -> listOf(
                "${storageDir.absolutePath}/$pluginName$pluginExtension",
                "/usr/local/lib/$pluginName$pluginExtension",
                "/usr/lib/$pluginName$pluginExtension",
                "/opt/homebrew/lib/$pluginName$pluginExtension"
            )
        }

        return locations.firstOrNull { File(it).exists() }
    }

    /**
     * Download DTT plugin from GitHub releases.
     */
    fun downloadPlugin(project: Project?, onComplete: (String?) -> Unit) {
        val asset = when {
            isWindows -> "DTTPlugin-win64.dll"
            isMacOS -> "libDTTPlugin-darwin.dylib"
            else -> "libDTTPlugin-linux.so"
        }

        val url = "https://github.com/adishavit/dtt/releases/$DTT_VERSION/download/$asset"
        val dest = File(storageDir, "$pluginName$pluginExtension")

        downloadFile(project, "Downloading DTT Plugin", url, dest) { success ->
            if (success) {
                if (!isWindows) dest.setExecutable(true)
                notify(project, "DTT plugin installed!", NotificationType.INFORMATION)
                onComplete(dest.absolutePath)
            } else {
                notify(project, "Failed to download DTT plugin", NotificationType.ERROR)
                onComplete(null)
            }
        }
    }

    /**
     * Download LLVM/Clang from official releases.
     */
    fun downloadLLVM(project: Project?, onComplete: (String?) -> Unit) {
        val arch = if (isMacOS) {
            val uname = Runtime.getRuntime().exec(arrayOf("uname", "-m")).inputStream.bufferedReader().readText().trim()
            if (uname == "arm64" || uname == "aarch64") "arm64" else "x86_64"
        } else "x86_64"

        val (filename, ext) = when {
            isWindows -> "LLVM-$LLVM_VERSION-win64" to ".zip"
            isMacOS -> "clang+llvm-$LLVM_VERSION-$arch-apple-darwin" to ".tar.xz"
            else -> "clang+llvm-$LLVM_VERSION-x86_64-linux-gnu-ubuntu-22.04" to ".tar.xz"
        }

        val url = "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VERSION/$filename$ext"
        val archiveFile = File(storageDir, "llvm$ext")
        val llvmDir = File(storageDir, "llvm")

        downloadFile(project, "Downloading LLVM $LLVM_VERSION", url, archiveFile) { success ->
            if (!success) {
                notify(project, "Failed to download LLVM", NotificationType.ERROR)
                onComplete(null)
                return@downloadFile
            }

            // Extract archive
            ProgressManager.getInstance().run(object : Task.Backgroundable(project, "Extracting LLVM...", false) {
                override fun run(indicator: ProgressIndicator) {
                    try {
                        llvmDir.mkdirs()
                        if (isWindows) {
                            // PowerShell extraction for Windows
                            ProcessBuilder(
                                "powershell", "-Command",
                                "Expand-Archive -Path '${archiveFile.absolutePath}' -DestinationPath '${llvmDir.absolutePath}' -Force"
                            ).start().waitFor()
                        } else {
                            // tar extraction for Unix
                            ProcessBuilder("tar", "-xf", archiveFile.absolutePath, "-C", llvmDir.absolutePath, "--strip-components=1")
                                .start().waitFor()
                        }
                        archiveFile.delete()

                        val clangPath = File(llvmDir, "bin/clang++" + if (isWindows) ".exe" else "")
                        if (clangPath.exists()) {
                            ApplicationManager.getApplication().invokeLater {
                                notify(project, "LLVM installed!", NotificationType.INFORMATION)
                                onComplete(clangPath.absolutePath)
                            }
                        } else {
                            // Windows ZIP may not strip components - check nested path
                            val nestedPath = File(llvmDir, "$filename/bin/clang++.exe")
                            if (isWindows && nestedPath.exists()) {
                                ApplicationManager.getApplication().invokeLater {
                                    notify(project, "LLVM installed!", NotificationType.INFORMATION)
                                    onComplete(nestedPath.absolutePath)
                                }
                            } else {
                                ApplicationManager.getApplication().invokeLater {
                                    notify(project, "LLVM extraction failed", NotificationType.ERROR)
                                    onComplete(null)
                                }
                            }
                        }
                    } catch (e: Exception) {
                        ApplicationManager.getApplication().invokeLater {
                            notify(project, "LLVM extraction failed: ${e.message}", NotificationType.ERROR)
                            onComplete(null)
                        }
                    }
                }
            })
        }
    }

    private fun downloadFile(project: Project?, title: String, url: String, dest: File, onComplete: (Boolean) -> Unit) {
        ProgressManager.getInstance().run(object : Task.Backgroundable(project, title, true) {
            override fun run(indicator: ProgressIndicator) {
                try {
                    dest.parentFile?.mkdirs()
                    var currentUrl = url
                    var redirects = 0

                    while (redirects < 10) {
                        val connection = URI(currentUrl).toURL().openConnection() as HttpURLConnection
                        connection.setRequestProperty("User-Agent", "DTT-CLion")
                        connection.instanceFollowRedirects = false

                        when (connection.responseCode) {
                            HttpURLConnection.HTTP_MOVED_TEMP, HttpURLConnection.HTTP_MOVED_PERM -> {
                                currentUrl = connection.getHeaderField("Location") ?: break
                                redirects++
                                connection.disconnect()
                                continue
                            }
                            HttpURLConnection.HTTP_OK -> {
                                val totalBytes = connection.contentLength
                                FileOutputStream(dest).use { fos ->
                                    connection.inputStream.use { input ->
                                        val buffer = ByteArray(8192)
                                        var downloaded = 0L
                                        var bytesRead: Int
                                        while (input.read(buffer).also { bytesRead = it } != -1) {
                                            if (indicator.isCanceled) {
                                                connection.disconnect()
                                                dest.delete()
                                                ApplicationManager.getApplication().invokeLater { onComplete(false) }
                                                return
                                            }
                                            fos.write(buffer, 0, bytesRead)
                                            downloaded += bytesRead
                                            if (totalBytes > 0) {
                                                indicator.fraction = downloaded.toDouble() / totalBytes
                                            }
                                        }
                                    }
                                }
                                connection.disconnect()
                                ApplicationManager.getApplication().invokeLater { onComplete(true) }
                                return
                            }
                            else -> {
                                connection.disconnect()
                                break
                            }
                        }
                    }
                    ApplicationManager.getApplication().invokeLater { onComplete(false) }
                } catch (e: Exception) {
                    ApplicationManager.getApplication().invokeLater { onComplete(false) }
                }
            }
        })
    }

    private fun notify(project: Project?, message: String, type: NotificationType) {
        NotificationGroupManager.getInstance()
            .getNotificationGroup("DTT Notifications")
            .createNotification("DTT", message, type)
            .notify(project)
    }
}
