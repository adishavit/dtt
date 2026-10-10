package com.dtt.clion

import com.intellij.execution.process.CapturingProcessHandler
import com.intellij.lang.annotation.AnnotationHolder
import com.intellij.lang.annotation.ExternalAnnotator
import com.intellij.lang.annotation.HighlightSeverity
import com.intellij.openapi.editor.Editor
import com.intellij.openapi.util.TextRange
import com.intellij.psi.PsiFile
import java.io.File
import java.nio.file.Files
import com.google.gson.Gson

data class DTTDiagnostic(
    val line: Int,
    val column: Int,
    val severity: String,
    val message: String
)

data class DTTInput(val file: PsiFile, val content: String)
data class DTTResult(val diagnostics: List<DTTDiagnostic>, val content: String? = null)

class DTTExternalAnnotator : ExternalAnnotator<DTTInput, DTTResult>() {

    override fun collectInformation(file: PsiFile, editor: Editor, hasErrors: Boolean): DTTInput? {
        val virtualFile = file.virtualFile ?: return null
        if (!isCppFile(virtualFile.name)) return null
        return DTTInput(file, file.text)
    }

    override fun doAnnotate(input: DTTInput): DTTResult {
        val settings = DTTSettings.getInstance().state

        val clangPath = findClang(settings.clangPath)
            ?: return DTTResult(listOf(DTTDiagnostic(1, 1, "warning", "DTT analysis unavailable: configure a matching Clang 18 and plugin.")), input.content)

        val pluginPath = findPlugin(settings.pluginPath)
            ?: return DTTResult(listOf(DTTDiagnostic(1, 1, "warning", "DTT analysis unavailable: configure a matching Clang 18 and plugin.")), input.content)

        val filePath = input.file.virtualFile?.path ?: return DTTResult(emptyList())

        if (!DTTBinarySetup.validateToolchain(clangPath, pluginPath)) {
            return DTTResult(listOf(DTTDiagnostic(1, 1, "warning",
                "DTT plugin load check failed. Use the matching Clang 18 build and Z3 runtime.")), input.content)
        }
        val result = runDTT(
            clangPath = clangPath,
            pluginPath = pluginPath,
            standard = settings.standard,
            mode = settings.mode,
            filePath = filePath,
            content = input.content
        )

        val diagnostics = parseDiagnostics(result, File(filePath).name)
        val failed = result.startsWith("DTT: Analysis") || result.lines().any {
            it.contains("error:") && !it.contains("DTT:")
        }
        return DTTResult(if (failed) listOf(DTTDiagnostic(1, 1, "warning", result)) else diagnostics, input.content)
    }

    override fun apply(file: PsiFile, result: DTTResult, holder: AnnotationHolder) {
        if (result.content != null && file.text != result.content) return
        val document = file.viewProvider.document ?: return
        val lineCount = document.lineCount

        for (diag in result.diagnostics) {
            // Validate line number is within document bounds
            val lineIndex = maxOf(0, minOf(diag.line - 1, lineCount - 1))
            if (lineIndex >= lineCount) continue

            val lineStart = document.getLineStartOffset(lineIndex)
            val lineEnd = document.getLineEndOffset(lineIndex)
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
        if (configPath.isNotBlank()) return configPath.takeIf { DTTBinarySetup.isCompatibleClang(it) }
        return DTTBinarySetup.findClang()
    }

    companion object {
        private const val TIMEOUT_SECONDS = 30L
    }

    private fun runDTT(clangPath: String, pluginPath: String, standard: String, mode: String, filePath: String, content: String?): String {
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

        var snapshot: File? = null
        return try {
            val args = cmd.toMutableList()
            if (content != null) {
                snapshot = Files.createTempDirectory("dtt-editor-").toFile()
                val source = File(snapshot, "source.cpp").apply { writeText(content) }
                val overlay = File(snapshot, "overlay.json")
                overlay.writeText(Gson().toJson(mapOf("version" to 0, "use-external-names" to false,
                    "roots" to listOf(mapOf("type" to "file", "name" to File(filePath).absolutePath,
                        "external-contents" to source.absolutePath)))))
                args.addAll(1, listOf("-ivfsoverlay", overlay.absolutePath))
            }
            val process = ProcessBuilder(args)
                .redirectErrorStream(true)
                .start()

            val output = CapturingProcessHandler(process, Charsets.UTF_8, cmd.joinToString(" "))
                .runProcess((TIMEOUT_SECONDS * 1000).toInt())

            if (output.isTimeout) {
                process.destroyForcibly()
                "DTT: Analysis timed out after ${TIMEOUT_SECONDS}s"
            } else if (output.exitCode != 0 && !output.stdout.contains("error: DTT:")) {
                "DTT: Analysis failed (exit ${output.exitCode})\n${output.stdout}"
            } else {
                output.stdout
            }
        } catch (e: Exception) {
            "DTT: Analysis failed - ${e.message}"
        } finally {
            snapshot?.deleteRecursively()
        }
    }

    private fun parseDiagnostics(output: String, filterFile: String): List<DTTDiagnostic> {
        // Pattern handles Windows paths correctly
        val pattern = Regex("""^([^:]+(?::[^:]+)*):(\d+):(\d+):\s*(error|warning|note):\s*(.+)$""")
        val maxLineCol = 10_000_000

        return output.lines()
            .mapNotNull { line ->
                pattern.matchEntire(line)?.let { match ->
                    val (file, lineStr, colStr, severity, message) = match.destructured

                    // Validate line and column numbers
                    val lineNum = lineStr.toIntOrNull() ?: return@mapNotNull null
                    val col = colStr.toIntOrNull() ?: return@mapNotNull null
                    if (lineNum < 1 || lineNum > maxLineCol || col < 1 || col > maxLineCol) {
                        return@mapNotNull null
                    }

                    // Filter to DTT diagnostics and matching file
                    val fileBasename = File(file).name
                    if (message.contains("DTT:") && (fileBasename == filterFile || file == filterFile)) {
                        DTTDiagnostic(lineNum, col, severity, message)
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
