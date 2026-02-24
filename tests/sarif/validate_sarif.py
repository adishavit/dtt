#!/usr/bin/env python3
"""Validate DTT SARIF output.

Usage:
    validate_sarif.py <sarif-file> [options]

Options:
    --expect-rule RULE_ID       Require at least one result with this rule ID
    --expect-min-results N      Require at least N results
    --expect-max-results N      Require at most N results
    --expect-no-results         Require zero results
    --expect-notes              Require at least one result with relatedLocations
"""

import json
import sys
import argparse

KNOWN_RULES = {
    "DTT-bounds-overflow", "DTT-null-deref", "DTT-use-after-free",
    "DTT-double-free", "DTT-uninitialized", "DTT-mismatched-dealloc",
    "DTT-resource-leak", "DTT-memop-overflow", "DTT-pointer-arith",
    "DTT-sprintf-unsafe", "DTT-union-access", "DTT-union-type-pun",
    "DTT-precondition", "DTT-enum-exhaustive", "DTT-annotation-conflict",
    "DTT-unverified-bounds", "DTT-unverified-precondition",
    "DTT-lost-tracking", "DTT-solver-failed",
}

VALID_LEVELS = {"error", "warning", "note", "none"}

def validate(path, args):
    errors = []

    with open(path) as f:
        try:
            data = json.load(f)
        except json.JSONDecodeError as e:
            print(f"FAIL: Invalid JSON: {e}")
            return 1

    # Schema and version
    if data.get("version") != "2.1.0":
        errors.append(f"Expected version 2.1.0, got {data.get('version')}")

    if "$schema" not in data:
        errors.append("Missing $schema")

    # Runs
    runs = data.get("runs", [])
    if len(runs) != 1:
        errors.append(f"Expected 1 run, got {len(runs)}")
        if not runs:
            for e in errors:
                print(f"FAIL: {e}")
            return 1

    run = runs[0]

    # Tool
    tool = run.get("tool", {}).get("driver", {})
    if tool.get("name") != "DTT":
        errors.append(f"Expected tool name 'DTT', got {tool.get('name')}")

    # Rules
    rules = tool.get("rules", [])
    rule_ids_in_tool = set()
    for rule in rules:
        rid = rule.get("id", "")
        rule_ids_in_tool.add(rid)
        if rid not in KNOWN_RULES:
            errors.append(f"Unknown rule ID in tool.rules: {rid}")
        # Check shortDescription
        if not rule.get("shortDescription", {}).get("text"):
            errors.append(f"Rule {rid} missing shortDescription.text")

    # Results
    results = run.get("results", [])

    for i, result in enumerate(results):
        rid = result.get("ruleId", "")
        if rid not in KNOWN_RULES:
            errors.append(f"Result[{i}]: unknown ruleId '{rid}'")
        if rid not in rule_ids_in_tool:
            errors.append(f"Result[{i}]: ruleId '{rid}' not declared in tool.rules")

        level = result.get("level", "")
        if level not in VALID_LEVELS:
            errors.append(f"Result[{i}]: invalid level '{level}'")

        # Message
        if not result.get("message", {}).get("text"):
            errors.append(f"Result[{i}]: missing message.text")

        # Locations
        locs = result.get("locations", [])
        if not locs:
            errors.append(f"Result[{i}]: missing locations")
        else:
            loc = locs[0].get("physicalLocation", {})
            uri = loc.get("artifactLocation", {}).get("uri", "")
            if uri.startswith("/"):
                errors.append(f"Result[{i}]: path should be relative, got '{uri}'")
            if not loc.get("artifactLocation", {}).get("uriBaseId"):
                errors.append(f"Result[{i}]: missing uriBaseId")
            region = loc.get("region", {})
            if region.get("startLine", 0) < 1:
                errors.append(f"Result[{i}]: startLine should be >= 1")

        # Partial fingerprints
        fp = result.get("partialFingerprints", {})
        if not fp.get("primaryLocationLineHash"):
            errors.append(f"Result[{i}]: missing partialFingerprints.primaryLocationLineHash")

    # Expectation checks
    if args.expect_rule:
        found = any(r.get("ruleId") == args.expect_rule for r in results)
        if not found:
            errors.append(f"Expected rule '{args.expect_rule}' not found in results")

    if args.expect_min_results is not None:
        if len(results) < args.expect_min_results:
            errors.append(f"Expected >= {args.expect_min_results} results, got {len(results)}")

    if args.expect_max_results is not None:
        if len(results) > args.expect_max_results:
            errors.append(f"Expected <= {args.expect_max_results} results, got {len(results)}")

    if args.expect_no_results:
        if len(results) > 0:
            errors.append(f"Expected 0 results, got {len(results)}")

    if args.expect_notes:
        has_notes = any(r.get("relatedLocations") for r in results)
        if not has_notes:
            errors.append("Expected at least one result with relatedLocations (notes)")

    # Report
    if errors:
        for e in errors:
            print(f"FAIL: {e}")
        return 1

    print(f"OK: {len(results)} result(s), {len(rule_ids_in_tool)} rule(s)")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Validate DTT SARIF output")
    parser.add_argument("sarif_file", help="Path to SARIF JSON file")
    parser.add_argument("--expect-rule", help="Require at least one result with this rule ID")
    parser.add_argument("--expect-min-results", type=int, help="Minimum number of results")
    parser.add_argument("--expect-max-results", type=int, help="Maximum number of results")
    parser.add_argument("--expect-no-results", action="store_true", help="Require zero results")
    parser.add_argument("--expect-notes", action="store_true", help="Require relatedLocations")
    args = parser.parse_args()

    sys.exit(validate(args.sarif_file, args))


if __name__ == "__main__":
    main()
