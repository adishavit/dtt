// Run against the compiled plugin and CLion SDK; arguments: fake compiler, work directory.
import com.dtt.clion.DTTExternalAnnotator
import java.io.File
import java.util.concurrent.TimeUnit

fun main(args: Array<String>) {
    val run = DTTExternalAnnotator::class.java.getDeclaredMethod(
        "runDTT", String::class.java, String::class.java, String::class.java,
        String::class.java, String::class.java)
    run.isAccessible = true
    val annotator = DTTExternalAnnotator()
    val work = File(args[1])
    for (hang in listOf(false, true)) {
        val pidFile = File(work, "child.pid")
        val config = File(work, "input.json")
        val behavior = if (hang) "\"ignoreTerm\":true" else
            "\"delay\":10,\"exitCode\":1,\"output\":\"test.cpp:1:1: error: DTT: test\\n\""
        config.writeText("{\"ready\":\"${pidFile.path}\",$behavior}")
        val start = System.nanoTime()
        try {
            val output = run.invoke(annotator, args[0], "plugin.so", "c++20", "enforce", config.path) as String
            if (hang) {
                check(output.contains("timed out")) { output }
                check(TimeUnit.NANOSECONDS.toSeconds(System.nanoTime() - start) < 35)
                val child = ProcessHandle.of(pidFile.readText().toLong())
                child.ifPresent { it.onExit().get(5, TimeUnit.SECONDS) }
            } else {
                check(output.contains("error: DTT: test")) { output }
            }
        } finally {
            if (pidFile.exists()) ProcessHandle.of(pidFile.readText().toLong()).ifPresent { it.destroyForcibly() }
        }
    }
    println("CLion compiler output and timeout checks passed")
    // Standalone SDK runs have no IDE lifecycle to shut down platform executors.
    kotlin.system.exitProcess(0)
}
