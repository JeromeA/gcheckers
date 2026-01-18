#!/usr/bin/env python3

import argparse
import html
import os
import sys
from dataclasses import dataclass
from typing import Iterable, List, Tuple


@dataclass
class CoverageLine:
  count: str
  line_no: str
  code: str

  @property
  def is_executable(self) -> bool:
    return self.count not in {"-", "====="}

  @property
  def is_covered(self) -> bool:
    return self.is_executable and self.count not in {"#####"}


@dataclass
class CoverageFile:
  name: str
  lines: List[CoverageLine]

  def totals(self) -> Tuple[int, int]:
    covered = 0
    executable = 0
    for line in self.lines:
      if not line.is_executable:
        continue
      executable += 1
      if line.is_covered:
        covered += 1
    return covered, executable


def parse_gcov_line(line: str) -> CoverageLine:
  parts = line.rstrip("\n").split(":", 2)
  if len(parts) < 3:
    return CoverageLine(count="-", line_no="", code=line.rstrip("\n"))
  count, line_no, code = parts
  return CoverageLine(count=count.strip(), line_no=line_no.strip(), code=code)


def load_gcov_file(path: str) -> CoverageFile:
  name = os.path.basename(path)
  with open(path, "r", encoding="utf-8") as handle:
    lines = [parse_gcov_line(line) for line in handle]
  return CoverageFile(name=name, lines=lines)


def iter_gcov_files(gcov_dir: str) -> Iterable[str]:
  for entry in sorted(os.listdir(gcov_dir)):
    if entry.endswith(".gcov"):
      yield os.path.join(gcov_dir, entry)


def render_file_report(output_dir: str, coverage_file: CoverageFile) -> str:
  output_name = coverage_file.name.replace(".gcov", ".html")
  output_path = os.path.join(output_dir, output_name)
  covered, executable = coverage_file.totals()
  percent = 100.0 if executable == 0 else covered / executable * 100
  line_rows = []
  for line in coverage_file.lines:
    classes = ["code-line"]
    if line.is_executable:
      classes.append("covered" if line.is_covered else "missed")
    line_rows.append(
      "<tr class=\"{classes}\"><td class=\"count\">{count}</td>"
      "<td class=\"line\">{line_no}</td><td class=\"code\"><pre>{code}</pre></td></tr>".format(
        classes=" ".join(classes),
        count=html.escape(line.count or ""),
        line_no=html.escape(line.line_no or ""),
        code=html.escape(line.code.rstrip("\n")),
      )
    )
  body = "".join(line_rows)
  document = """
<!DOCTYPE html>
<html lang=\"en\">
<head>
  <meta charset=\"utf-8\">
  <title>Coverage for {name}</title>
  <style>
    body {{ font-family: Arial, sans-serif; margin: 20px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    th, td {{ border: 1px solid #ddd; padding: 4px 6px; vertical-align: top; }}
    th {{ background: #f4f4f4; }}
    tr.covered td {{ background: #e8f6e8; }}
    tr.missed td {{ background: #fbeaea; }}
    td.count, td.line {{ text-align: right; width: 60px; white-space: nowrap; }}
    td.code pre {{ margin: 0; white-space: pre-wrap; }}
  </style>
</head>
<body>
  <h1>Coverage for {name}</h1>
  <p>Covered lines: {covered} / {executable} ({percent:.2f}%)</p>
  <table>
    <thead>
      <tr><th>Count</th><th>Line</th><th>Code</th></tr>
    </thead>
    <tbody>
      {body}
    </tbody>
  </table>
  <p><a href=\"index.html\">Back to summary</a></p>
</body>
</html>
""".format(
    name=html.escape(coverage_file.name.replace(".gcov", "")),
    covered=covered,
    executable=executable,
    percent=percent,
    body=body,
  )
  with open(output_path, "w", encoding="utf-8") as handle:
    handle.write(document)
  return output_name


def render_index(output_dir: str, reports: List[Tuple[CoverageFile, str]]) -> None:
  rows = []
  for coverage_file, report_name in reports:
    covered, executable = coverage_file.totals()
    percent = 100.0 if executable == 0 else covered / executable * 100
    rows.append(
      "<tr><td><a href=\"{report}\">{name}</a></td><td>{covered}</td><td>{executable}</td>"
      "<td>{percent:.2f}%</td></tr>".format(
        report=html.escape(report_name),
        name=html.escape(coverage_file.name.replace(".gcov", "")),
        covered=covered,
        executable=executable,
        percent=percent,
      )
    )
  body = "".join(rows)
  document = """
<!DOCTYPE html>
<html lang=\"en\">
<head>
  <meta charset=\"utf-8\">
  <title>Coverage Summary</title>
  <style>
    body {{ font-family: Arial, sans-serif; margin: 20px; }}
    table {{ border-collapse: collapse; width: 100%; }}
    th, td {{ border: 1px solid #ddd; padding: 6px; text-align: left; }}
    th {{ background: #f4f4f4; }}
  </style>
</head>
<body>
  <h1>Coverage Summary</h1>
  <table>
    <thead>
      <tr><th>File</th><th>Covered</th><th>Executable</th><th>Coverage</th></tr>
    </thead>
    <tbody>
      {body}
    </tbody>
  </table>
</body>
</html>
""".format(body=body)
  with open(os.path.join(output_dir, "index.html"), "w", encoding="utf-8") as handle:
    handle.write(document)


def main(argv: List[str]) -> int:
  parser = argparse.ArgumentParser(description="Generate HTML coverage report from gcov output.")
  parser.add_argument("--gcov-dir", required=True, help="Directory containing .gcov files.")
  parser.add_argument("--output-dir", required=True, help="Directory to write HTML reports.")
  args = parser.parse_args(argv)

  gcov_dir = args.gcov_dir
  output_dir = args.output_dir
  os.makedirs(output_dir, exist_ok=True)

  reports = []
  for gcov_path in iter_gcov_files(gcov_dir):
    coverage_file = load_gcov_file(gcov_path)
    report_name = render_file_report(output_dir, coverage_file)
    reports.append((coverage_file, report_name))

  if not reports:
    raise SystemExit("No .gcov files found in {}".format(gcov_dir))

  render_index(output_dir, reports)
  return 0


if __name__ == "__main__":
  raise SystemExit(main(sys.argv[1:]))
