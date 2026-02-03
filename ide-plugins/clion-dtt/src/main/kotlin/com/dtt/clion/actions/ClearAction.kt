package com.dtt.clion.actions

import com.intellij.openapi.actionSystem.AnAction
import com.intellij.openapi.actionSystem.AnActionEvent
import com.intellij.openapi.actionSystem.CommonDataKeys
import com.intellij.codeInsight.daemon.DaemonCodeAnalyzer

class ClearAction : AnAction() {
    override fun actionPerformed(e: AnActionEvent) {
        val project = e.project ?: return
        val psiFile = e.getData(CommonDataKeys.PSI_FILE) ?: return

        // Restart daemon to clear annotations
        DaemonCodeAnalyzer.getInstance(project).restart(psiFile)
    }
}
