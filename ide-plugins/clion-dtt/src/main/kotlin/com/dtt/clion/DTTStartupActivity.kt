package com.dtt.clion

import com.intellij.notification.NotificationAction
import com.intellij.notification.NotificationGroupManager
import com.intellij.notification.NotificationType
import com.intellij.openapi.project.Project
import com.intellij.openapi.startup.ProjectActivity

/**
 * Checks for DTT binaries on startup and prompts for download if missing.
 */
class DTTStartupActivity : ProjectActivity {
    override suspend fun execute(project: Project) {
        val settings = DTTSettings.getInstance().state

        // Skip if auto-download is disabled
        if (!settings.autoDownload) return

        val clangPath = if (settings.clangPath.isNotBlank()) settings.clangPath else DTTBinarySetup.findClang()
        val pluginPath = if (settings.pluginPath.isNotBlank()) settings.pluginPath else DTTBinarySetup.findPlugin()

        // If plugin is missing, prompt for download
        if (pluginPath == null) {
            promptPluginDownload(project)
        }

        // If clang is missing, prompt for download
        if (clangPath == null) {
            promptClangDownload(project)
        }
    }

    private fun promptPluginDownload(project: Project) {
        NotificationGroupManager.getInstance()
            .getNotificationGroup("DTT Notifications")
            .createNotification(
                "DTT Plugin Not Found",
                "DTT plugin binary is required for memory safety analysis. Download now?",
                NotificationType.WARNING
            )
            .addAction(NotificationAction.createSimple("Download") {
                DTTBinarySetup.downloadPlugin(project) { path ->
                    if (path != null) {
                        DTTSettings.getInstance().state.pluginPath = path
                    }
                }
            })
            .addAction(NotificationAction.createSimple("Configure Manually") {
                com.intellij.openapi.options.ShowSettingsUtil.getInstance()
                    .showSettingsDialog(project, "DTT")
            })
            .notify(project)
    }

    private fun promptClangDownload(project: Project) {
        NotificationGroupManager.getInstance()
            .getNotificationGroup("DTT Notifications")
            .createNotification(
                "Clang Not Found",
                "DTT requires Clang 18+ for analysis. Download LLVM now?",
                NotificationType.WARNING
            )
            .addAction(NotificationAction.createSimple("Download LLVM") {
                DTTBinarySetup.downloadLLVM(project) { path ->
                    if (path != null) {
                        DTTSettings.getInstance().state.clangPath = path
                    }
                }
            })
            .addAction(NotificationAction.createSimple("Configure Manually") {
                com.intellij.openapi.options.ShowSettingsUtil.getInstance()
                    .showSettingsDialog(project, "DTT")
            })
            .notify(project)
    }
}
