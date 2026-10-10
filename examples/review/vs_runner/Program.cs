using System;
using System.IO;
using System.Threading.Tasks;
using DTT;

class Program
{
    static async Task Main(string[] args)
    {
        var dir = Path.Combine(Path.GetTempPath(), "dtt-vs-test-" + Guid.NewGuid());
        Directory.CreateDirectory(dir);
        try
        {
            var source = Path.Combine(dir, "input with spaces.cpp");
            File.WriteAllText(Path.Combine(dir, "local.h"), "#include <vector>");
            Func<bool, string> code = bad => "#include \"local.h\"\nvoid f(){std::vector<int> v" + (bad ? "" : "{1}") + "; v.front();}";
            var compatible = typeof(Runner).GetMethod("IsCompatibleClangAsync");
            var probe = typeof(Runner).GetMethod("ProbeAsync");
            if (compatible == null || probe == null) throw new Exception("Missing toolchain validation");
            if (!await (Task<bool>)compatible.Invoke(null, new object[] { "/usr/bin/clang++-18" })) throw new Exception("Valid Clang rejected");
            if (await (Task<bool>)compatible.Invoke(null, new object[] { "/missing/compiler" })) throw new Exception("Missing Clang accepted");
            var wrong = Path.Combine(dir, "wrong-clang");
            File.WriteAllText(wrong, "#!/bin/sh\necho 'clang version 19.0.0'\n");
            if (!OperatingSystem.IsWindows()) File.SetUnixFileMode(wrong, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
            if (await (Task<bool>)compatible.Invoke(null, new object[] { wrong })) throw new Exception("Wrong Clang accepted");
            using (var runner = new Runner())
            {
                if (!await (Task<bool>)probe.Invoke(runner, new object[] { new Runner.Config("/usr/bin/clang++-18", args[0], "c++20", "inform") }))
                    throw new Exception("Valid plugin rejected");
                if (await (Task<bool>)probe.Invoke(runner, new object[] { new Runner.Config("/usr/bin/clang++-18", source, "c++20", "inform") }))
                    throw new Exception("Missing plugin accepted");
                foreach (var bad in new[] { true, false })
                {
                    File.WriteAllText(source, code(!bad));
                    var config = new Runner.Config("/usr/bin/clang++-18", args[0], "c++20", "enforce");
                    var property = typeof(Runner.Config).GetProperty("Content");
                    if (property == null) throw new Exception("Runner cannot accept an unsaved snapshot");
                    property.SetValue(config, code(bad));
                    var result = await runner.RunAsync(source, config);
                    if (result.ExitCode != (bad ? 1 : 0)) throw new Exception(result.Output);
                    if (bad && !result.Output.Contains(source + ":2:")) throw new Exception(result.Output);
                    if (File.ReadAllText(source) != code(!bad)) throw new Exception("Saved source changed");
                }
            }
            Console.WriteLine("Visual Studio runner snapshots and local includes passed");
        }
        finally { Directory.Delete(dir, true); }
    }
}
