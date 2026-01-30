package com.dtt.clion.actions

import com.intellij.openapi.actionSystem.AnAction
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.CommonDataKeys
import com.intellij.codeInsight.daemon.DaemonCodeAnalyzer

class AnalyzeAction : AnAction() {
    override fun actionPerformed(e: AnActionEvent) {
        val project = e.project ?: return
        val psiFile = e.getData(CommonDataKeys.PSI_FILE) ?: return

        // Trigger re-analysis by restarting the daemon
        DaemonCodeAnalyzer.getInstance(project).restart(psiFile)
    }

    override fun update(e: AnActionEvent) {
        val file = e.getData(CommonDataKeys.VIRTUAL_FILE)
        e.presentation.isEnabled = file != null && isCppFile(file.name)
    }

    private fun isCppFile(name: String): Boolean {
        val ext = name.substringAfterLast('.', "").lowercase()
        return ext in listOf("cpp", "cxx", "cc", "c", "hpp", "hxx", "h")
    }
}
