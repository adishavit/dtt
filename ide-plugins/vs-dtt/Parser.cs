using System.Collections.Generic;
using System.Text.RegularExpressions;

namespace DTT;

/// <summary>
/// DTT Diagnostic Parser - Pure functions, no IDE dependencies
/// </summary>
public static class Parser
{
    public enum Severity { Error, Warning, Note }

    public record Diagnostic(
        string File,
        int Line,
        int Column,
        Severity Severity,
        string Message
    );

    // Clang format: file:line:col: severity: message
    private static readonly Regex Pattern = new(
        @"^(.+?):(\d+):(\d+):\s*(error|warning|note):\s*(.+)$",
        RegexOptions.Compiled
    );

    /// <summary>Parse clang stderr into structured diagnostics</summary>
    public static IEnumerable<Diagnostic> Parse(string output, string? filterFile = null)
    {
        foreach (var line in output.Split('\n'))
        {
            var match = Pattern.Match(line.Trim());
            if (!match.Success) continue;

            var file = match.Groups[1].Value;
            var lineNum = int.Parse(match.Groups[2].Value);
            var col = int.Parse(match.Groups[3].Value);
            var severity = ParseSeverity(match.Groups[4].Value);
            var message = match.Groups[5].Value;

            // Only DTT diagnostics
            if (!message.Contains("DTT:")) continue;

            // Filter to file if specified
            if (filterFile != null && !file.EndsWith(filterFile) && file != filterFile)
                continue;

            yield return new Diagnostic(file, lineNum, col, severity, message);
        }
    }

    private static Severity ParseSeverity(string s) => s switch
    {
        "error" => Severity.Error,
        "warning" => Severity.Warning,
        _ => Severity.Note
    };
}
