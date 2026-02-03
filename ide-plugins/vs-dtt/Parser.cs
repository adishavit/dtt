using System.Collections.Generic;
using System.Text.RegularExpressions;

namespace DTT
{
    /// <summary>
    /// DTT Diagnostic Parser - Pure functions, no IDE dependencies
    /// </summary>
    public static class Parser
    {
        public enum Severity { Error, Warning, Note }

        public class Diagnostic
        {
            public string File { get; }
            public int Line { get; }
            public int Column { get; }
            public Severity Severity { get; }
            public string Message { get; }

            public Diagnostic(string file, int line, int column, Severity severity, string message)
            {
                File = file;
                Line = line;
                Column = column;
                Severity = severity;
                Message = message;
            }
        }

        // Clang format: file:line:col: severity: message
        // Uses pattern that handles Windows paths (C:\...) correctly
        private static readonly Regex Pattern = new Regex(
            @"^([^:]+(?::[^:]+)*):(\d+):(\d+):\s*(error|warning|note):\s*(.+)$",
            RegexOptions.Compiled
        );

        // Maximum safe value for line/column numbers
        private const int MaxLineCol = 10000000;

        /// <summary>Parse clang stderr into structured diagnostics</summary>
        public static IEnumerable<Diagnostic> Parse(string output, string filterFile = null)
        {
            // Handle both Unix and Windows line endings
            foreach (var rawLine in output.Split(new[] { '\r', '\n' }, System.StringSplitOptions.RemoveEmptyEntries))
            {
                var match = Pattern.Match(rawLine.Trim());
                if (!match.Success) continue;

                var file = match.Groups[1].Value;
                var message = match.Groups[5].Value;

                // Validate line and column numbers
                int lineNum, col;
                if (!int.TryParse(match.Groups[2].Value, out lineNum) ||
                    !int.TryParse(match.Groups[3].Value, out col) ||
                    lineNum < 1 || lineNum > MaxLineCol ||
                    col < 1 || col > MaxLineCol)
                {
                    continue;
                }

                var severity = ParseSeverity(match.Groups[4].Value);

                // Only DTT diagnostics
                if (!message.Contains("DTT:")) continue;

                // Filter to file if specified - use case-insensitive comparison on Windows
                if (filterFile != null)
                {
                    var fileBasename = System.IO.Path.GetFileName(file);
                    if (!fileBasename.Equals(filterFile, System.StringComparison.OrdinalIgnoreCase) &&
                        !file.Equals(filterFile, System.StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }
                }

                yield return new Diagnostic(file, lineNum, col, severity, message);
            }
        }

        private static Severity ParseSeverity(string s)
        {
            switch (s)
            {
                case "error": return Severity.Error;
                case "warning": return Severity.Warning;
                default: return Severity.Note;
            }
        }
    }
}
