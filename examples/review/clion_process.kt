// Run against the compiled plugin and CLion SDK; arguments: fake compiler, work directory.
import com.dtt.clion.DTTExternalAnnotator
import java.io.File
import java.util.concurrent.TimeUnit

fun main(args: Array<String>) {
    val setup = Class.forName("com.dtt.clion.DTTBinarySetup")
    val validate = setup.getMethod("validateToolchain", String::class.java, String::class.java)
    val instance = setup.getField("INSTANCE").get(null)
    check(validate.invoke(instance, "/usr/bin/clang++-18", args[2]) == true)
    check(validate.invoke(instance, "/usr/bin/clang++-18", args[0]) == false)
    check(validate.invoke(instance, "/missing/compiler", args[2]) == false)
    val wrong = File(args[1], "wrong-clang")
    wrong.writeText("#!/bin/sh\necho 'clang version 19.0.0'\n")
    wrong.setExecutable(true)
    check(validate.invoke(instance, wrong.path, args[2]) == false)
    val run = DTTExternalAnnotator::class.java.getDeclaredMethod(
        "runDTT", String::class.java, String::class.java, String::class.java,
        String::class.java, String::class.java, String::class.java)
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
            val output = run.invoke(annotator, args[0], "plugin.so", "c++20", "enforce", config.path, null) as String
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
    val source = File(work, "unsaved.cpp")
    File(work, "local.h").writeText("#include <vector>")
    fun code(bad: Boolean) = "#include \"local.h\"\nvoid f(){std::vector<int> v" +
        (if (bad) "" else "{1}") + "; v.front();}"
    for (bad in listOf(true, false)) {
        source.writeText(code(!bad))
        val output = run.invoke(annotator, "/usr/bin/clang++-18", args[2], "c++20", "enforce", source.path, code(bad)) as String
        check(output.contains("error: DTT:") == bad) { output }
        check(source.readText() == code(!bad))
    }
    println("CLion compiler output and timeout checks passed")
    // Standalone SDK runs have no IDE lifecycle to shut down platform executors.
    kotlin.system.exitProcess(0)
}
