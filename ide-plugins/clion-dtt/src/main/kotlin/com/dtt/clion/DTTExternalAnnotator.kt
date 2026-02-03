package com.dtt.clion

import com.intellij.lang.annotation.AnnotationHolder
import com.intellij.lang.annotation.ExternalAnnotator
import com.intellij.lang.annotation.HighlightSeverity
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.util.TextRange
import com.intellij.psi.PsiFile
import java.io.File
import java.util.concurrent.TimeUnit

data class DTTDiagnostic(
    val line: Int,
    val column: Int,
    val severity: String,
    val message: String
)

data class DTTInput(val file: PsiFile, val content: String)
data class DTTResult(val diagnostics: List<DTTDiagnostic>)

class DTTExternalAnnotator : ExternalAnnotator<DTTInput, DTTResult>() {

    override fun collectInformation(file: PsiFile, editor: Editor, hasErrors: Boolean): DTTInput? {
        val virtualFile = file.virtualFile ?: return null
        if (!isCppFile(virtualFile.name)) return null
        return DTTInput(file, file.text)
    }

    override fun doAnnotate(input: DTTInput): DTTResult {
        val settings = DTTSettings.getInstance().state

        val clangPath = findClang(settings.clangPath)
            ?: return DTTResult(emptyList())

        val pluginPath = findPlugin(settings.pluginPath)
            ?: return DTTResult(emptyList())

        val filePath = input.file.virtualFile?.path ?: return DTTResult(emptyList())

        val result = runDTT(
            clangPath = clangPath,
            pluginPath = pluginPath,
            standard = settings.standard,
            mode = settings.mode,
            filePath = filePath
        )

        return DTTResult(parseDiagnostics(result, File(filePath).name))
    }

    override fun apply(file: PsiFile, result: DTTResult, holder: AnnotationHolder) {
        val document = file.viewProvider.document ?: return

        for (diag in result.diagnostics) {
            val lineStart = document.getLineStartOffset(maxOf(0, diag.line - 1))
            val lineEnd = document.getLineEndOffset(maxOf(0, diag.line - 1))
            val col = maxOf(0, diag.column - 1)

            val startOffset = minOf(lineStart + col, lineEnd)
            val endOffset = minOf(startOffset + findWordLength(file.text, startOffset), lineEnd)

            val range = TextRange(startOffset, maxOf(startOffset + 1, endOffset))
            val severity = when (diag.severity) {
                "error" -> HighlightSeverity.ERROR
                "warning" -> HighlightSeverity.WARNING
                else -> HighlightSeverity.INFORMATION
            }

            holder.newAnnotation(severity, diag.message)
                .range(range)
                .create()
        }
    }

    private fun isCppFile(name: String): Boolean {
        val ext = name.substringAfterLast('.', "").lowercase()
        return ext in listOf("cpp", "cxx", "cc", "c", "hpp", "hxx", "h")
    }

    private fun findPlugin(configPath: String): String? {
        if (configPath.isNotBlank() && File(configPath).exists()) {
            return configPath
        }
        return DTTBinarySetup.findPlugin()
    }

    private fun findClang(configPath: String): String? {
        if (configPath.isNotBlank()) {
            try {
                val process = ProcessBuilder(listOf(configPath, "--version"))
                    .redirectErrorStream(true)
                    .start()
                if (process.waitFor() == 0) return configPath
            } catch (_: Exception) { }
        }
        return DTTBinarySetup.findClang()
    }

    companion object {
        private const val TIMEOUT_SECONDS = 30L
    }

    private fun runDTT(clangPath: String, pluginPath: String, standard: String, mode: String, filePath: String): String {
        val isWindows = System.getProperty("os.name").lowercase().contains("win")

        val cmd = if (isWindows) {
            listOf(clangPath, "-Xclang", "-load", "-Xclang", pluginPath,
                "-Xclang", "-add-plugin", "-Xclang", "dtt",
                "-Xclang", "-plugin-arg-dtt", "-Xclang", "--dtt-mode=$mode",
                "-std=$standard", "-fsyntax-only", filePath)
        } else {
            listOf(clangPath, "-fplugin=$pluginPath",
                "-fplugin-arg-dtt---dtt-mode=$mode",
                "-std=$standard", "-fsyntax-only", filePath)
        }

        return try {
            val process = ProcessBuilder(cmd)
                .redirectErrorStream(true)
                .start()

            val output = process.inputStream.bufferedReader().readText()
            val completed = process.waitFor(TIMEOUT_SECONDS, TimeUnit.SECONDS)

            if (!completed) {
                process.destroyForcibly()
                "DTT: Analysis timed out after ${TIMEOUT_SECONDS}s"
            } else {
                output
            }
        } catch (e: Exception) {
            "DTT: Analysis failed - ${e.message}"
        }
    }

    private fun parseDiagnostics(output: String, filterFile: String): List<DTTDiagnostic> {
        val pattern = Regex("""^(.+?):(\d+):(\d+):\s*(error|warning|note):\s*(.+)$""")
        return output.lines()
            .mapNotNull { line ->
                pattern.matchEntire(line)?.let { match ->
                    val (file, lineNum, col, severity, message) = match.destructured
                    if (message.contains("DTT:") && (file.endsWith(filterFile) || file == filterFile)) {
                        DTTDiagnostic(lineNum.toInt(), col.toInt(), severity, message)
                    } else null
                }
            }
    }

    private fun findWordLength(text: String, offset: Int): Int {
        var len = 0
        var i = offset
        while (i < text.length && (text[i].isLetterOrDigit() || text[i] == '_')) {
            len++
            i++
        }
        return maxOf(len, 1)
    }
}
