#!/usr/bin/env python3

from __future__ import annotations

import argparse
import difflib
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent
# The floor is what the suite really reaches, so the task refuses a fall rather than an unmet ambition.
COVERAGE_LINE_FLOOR = 81


@dataclass(frozen=True)
class Context:
    configuration: str
    build_dir: Path
    jobs: int
    verbose: bool
    value: str | None = None


def run(command: list[str], *, cwd: Path = ROOT, env: dict[str, str] | None = None) -> None:
    printable = " ".join(command)
    print(f"\n> {printable}", flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def executable(name: str) -> str:
    resolved = shutil.which(name)
    if resolved is None:
        raise RuntimeError(f"Required executable was not found: {name}")
    return resolved


# The Qt installer writes one directory per version, each holding one directory per toolchain it was built for.
QT_INSTALL_ROOTS = (Path.home() / "Qt", Path("C:/Qt"), Path("/opt/Qt"))
QT_ENVIRONMENT_NAMES = ("CMAKE_PREFIX_PATH", "Qt6_DIR", "QT_ROOT_DIR")


def required_qt_version() -> tuple[int, ...]:
    match = re.search(r"find_package\(Qt6 (\d+(?:\.\d+)*) REQUIRED", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if match is None:
        raise RuntimeError("The Qt requirement was not found in CMakeLists.txt")
    return tuple(int(part) for part in match.group(1).split("."))


def qt_named_by_environment() -> bool:
    return any(os.environ.get(name) for name in QT_ENVIRONMENT_NAMES)


# A developer or a workflow that named a Qt has chosen one, so the installed layout is only read when nobody did.
def installed_qt() -> Path | None:
    if qt_named_by_environment():
        return None

    required = required_qt_version()
    found: list[tuple[tuple[int, ...], Path]] = []

    for root in QT_INSTALL_ROOTS:
        if not root.is_dir():
            continue

        for entry in sorted(root.iterdir()):
            parts = entry.name.split(".")

            if not entry.is_dir() or not all(part.isdigit() for part in parts):
                continue

            version = tuple(int(part) for part in parts)

            if version < required:
                continue

            for build in sorted(entry.iterdir()):
                if (build / "lib" / "cmake" / "Qt6").is_dir():
                    found.append((version, build))

    return max(found)[1] if found else None


def cmake_configure(context: Context, *definitions: str) -> None:
    qt = installed_qt()
    command = [
        executable("cmake"),
        "-S",
        str(ROOT),
        "-B",
        str(context.build_dir),
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={context.configuration}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        *([f"-DCMAKE_PREFIX_PATH={qt}"] if qt is not None else []),
        *definitions,
    ]
    run(command)


def cmake_build(context: Context, *targets: str) -> None:
    command = [executable("cmake"), "--build", str(context.build_dir), "--parallel", str(context.jobs)]
    if targets:
        command.extend(["--target", *targets])
    if context.verbose:
        command.append("--verbose")
    run(command)


def task_doctor(_: Context) -> None:
    tools = ["cmake", "ninja", "zig", "clang-format", "cppcheck"]
    optional = ["gcovr"]
    for name in tools:
        print(f"{name}: {executable(name)}")
    for name in optional:
        print(f"{name}: {shutil.which(name) or 'not installed'}")

    declared = ".".join(str(part) for part in required_qt_version())
    qt = installed_qt()

    if qt is not None:
        print(f"qt: {qt}")
        return
    if qt_named_by_environment():
        print(f"qt: named by {', '.join(name for name in QT_ENVIRONMENT_NAMES if os.environ.get(name))}")
        return

    raise RuntimeError(f"Qt {declared} or newer was not found, so name one through CMAKE_PREFIX_PATH or install it where the Qt installer writes it")


def task_configure(context: Context) -> None:
    cmake_configure(context)


def task_build(context: Context) -> None:
    if not (context.build_dir / "CMakeCache.txt").exists():
        task_configure(context)
    cmake_build(context)


def app_path(context: Context) -> Path:
    if sys.platform == "darwin":
        return context.build_dir / "src" / "Workpane.app" / "Contents" / "MacOS" / "Workpane"
    suffix = ".exe" if os.name == "nt" else ""
    return context.build_dir / "src" / f"Workpane{suffix}"


def task_run(context: Context) -> None:
    task_build(context)
    run([str(app_path(context))])


def task_test(context: Context) -> None:
    cmake_configure(context, "-DWORKPANE_BUILD_TESTS=ON")
    cmake_build(context)
    command = [
        executable("ctest"),
        "--test-dir",
        str(context.build_dir),
        "--parallel",
        str(context.jobs),
        "--output-on-failure",
        "--no-tests=error",
    ]
    if context.verbose:
        command.append("--verbose")
    run(command)


def task_coverage(context: Context) -> None:
    coverage_context = Context("Debug", ROOT / "build" / "coverage", context.jobs, context.verbose)
    gcovr = executable("gcovr")
    cmake_configure(
        coverage_context,
        "-DWORKPANE_BUILD_TESTS=ON",
        "-DWORKPANE_ENABLE_COVERAGE=ON",
    )
    cmake_build(coverage_context)

    # The counters of a previous run belong to the sources as they were then, so a report merged with them describes neither build.
    for stale in coverage_context.build_dir.rglob("*.gcda"):
        stale.unlink()

    run([
        executable("ctest"),
        "--test-dir",
        str(coverage_context.build_dir),
        "--parallel",
        str(coverage_context.jobs),
        "--output-on-failure",
        "--no-tests=error",
    ])
    report_dir = coverage_context.build_dir / "coverage"
    report_dir.mkdir(parents=True, exist_ok=True)
    # A clang toolchain writes the same data through a tool of its own, so gcovr is told which one reads it.
    compiler = ""

    for line in (coverage_context.build_dir / "CMakeCache.txt").read_text(encoding="utf-8").split("\n"):
        if line.startswith("CMAKE_CXX_COMPILER_ID:"):
            compiler = line.split("=", 1)[1]

    reader = ["--gcov-executable", "llvm-cov gcov"] if "Clang" in compiler else []
    run([
        gcovr,
        *reader,
        "--root",
        str(ROOT),
        "--filter",
        str(ROOT / "src"),
        "--filter",
        str(ROOT / "plugins"),
        "--exclude-unreachable-branches",
        "--html-details",
        str(report_dir / "index.html"),
        "--xml",
        str(report_dir / "cobertura.xml"),
        "--fail-under-line",
        str(COVERAGE_LINE_FLOOR),
        str(coverage_context.build_dir),
    ])


def source_files() -> list[str]:
    extensions = {".c", ".cc", ".cpp", ".h", ".hpp"}
    roots = [ROOT / "src", ROOT / "plugins", ROOT / "tests"]
    return [str(path) for base in roots if base.exists() for path in base.rglob("*") if path.suffix in extensions]


def task_format(_: Context) -> None:
    files = source_files()
    if files:
        run([executable("clang-format"), "-i", *files])


def task_format_check(_: Context) -> None:
    files = source_files()
    if files:
        run([executable("clang-format"), "--dry-run", "--Werror", *files])


LAMBDA_PATTERN = re.compile(r"\[([^\]\[]*)\]\s*(\([^)]*\))?\s*(mutable\s*)?(->\s*[A-Za-z_:<>, ]+)?\s*\{")
CAPTURE_PATTERN = re.compile(r"[&=]?\s*[A-Za-z_&=, .*]*")
TEXT_LITERAL_PATTERN = re.compile(r"\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*'")


def unprotected_lambdas() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            protected = False

            for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
                stripped = line.strip()

                if stripped == "// clang-format off":
                    protected = True
                    continue

                if stripped == "// clang-format on":
                    protected = False
                    continue

                if protected or stripped.startswith("//"):
                    continue

                for match in LAMBDA_PATTERN.finditer(TEXT_LITERAL_PATTERN.sub('""', line)):
                    capture = match.group(1)

                    if capture and not CAPTURE_PATTERN.fullmatch(capture):
                        continue

                    found.append(f"{path.relative_to(ROOT)}:{number}")

    return found


def translation_placeholders() -> dict[str, int]:
    declared: dict[str, int] = {}
    catalogs = sorted(ROOT.glob("plugins/*/*Translations.h")) + [ROOT / "src" / "plugins" / "CoreTranslations.h"]

    for path in catalogs:
        for key, value in re.findall(r"\{QStringLiteral\(\"([a-z0-9.\-]+)\"\), QStringLiteral\(\"((?:[^\"\\\\]|\\\\.)*)\"\)\}", path.read_text(encoding="utf-8")):
            marks = [int(mark) for mark in re.findall(r"%(\d)", value)]
            declared[key] = max(declared.get(key, 0), max(marks) if marks else 0)

    return declared


def given_arguments(text: str, start: int) -> int:
    total = 0
    index = start

    while True:
        opening = re.match(r"\s*\.arg\(", text[index:])
        if opening is None:
            return total
        index += opening.end()
        depth = 1
        pieces = 1

        while index < len(text) and depth:
            character = text[index]
            if character in "([{":
                depth += 1
            elif character in ")]}":
                depth -= 1
                if depth == 0:
                    break
            elif character == "," and depth == 1:
                pieces += 1
            index += 1

        index += 1
        total += pieces


def mismatched_translations() -> list[str]:
    declared = translation_placeholders()
    found: list[str] = []

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h") or path.name.endswith("Translations.h"):
                continue

            text = path.read_text(encoding="utf-8")

            for match in re.finditer(r"translate\(QStringLiteral\(\"([a-z0-9.\-]+)\"\)\)", text):
                key = match.group(1)
                if key in declared and given_arguments(text, match.end()) != declared[key]:
                    found.append(f"{path.relative_to(ROOT)}:{text[:match.start()].count(chr(10)) + 1} {key}")

            # A call that chooses between two sentences gives the same arguments to both, so both must take the same.
            for match in re.finditer(r"translate\([^()]*\?[^()]*QStringLiteral\(\"([a-z0-9.\-]+)\"\)[^()]*:[^()]*QStringLiteral\(\"([a-z0-9.\-]+)\"\)\)", text):
                first = declared.get(match.group(1))
                second = declared.get(match.group(2))
                if first is not None and second is not None and first != second:
                    found.append(f"{path.relative_to(ROOT)}:{text[:match.start()].count(chr(10)) + 1} {match.group(1)} and {match.group(2)}")

    return found


def unguarded_continuations() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            lines = path.read_text(encoding="utf-8").split("\n")

            for number, line in enumerate(lines, 1):
                for match in re.finditer(r"\.then\(\s*\[([^\]]*)\]", line):
                    if match.group(1).strip():
                        found.append(f"{path.relative_to(ROOT)}:{number}")

    return found


def unlisted_icons() -> list[str]:
    declaration = (ROOT / "src" / "ui" / "Icons.h").read_text(encoding="utf-8")
    accessor = (ROOT / "src" / "ui" / "Icons.cpp").read_text(encoding="utf-8")
    body = re.search(r"enum class IconName[^{]*\{(.*?)\};", declaration, re.S)
    listing = re.search(r"allIconNames\(\)[^{]*\{(.*?)\n\}", accessor, re.S)

    if body is None or listing is None:
        raise RuntimeError("The icon enumeration and its accessor could not be read")

    declared = [match.group(1) for match in re.finditer(r"(\w+)", body.group(1))]
    listed = re.findall(r"IconName::(\w+)", listing.group(1))
    return sorted(set(declared).symmetric_difference(listed))


def inherited_catalogs() -> list[str]:
    found: list[str] = []

    for path in sorted(ROOT.glob("plugins/*/*Translations.h")) + [ROOT / "src" / "plugins" / "CoreTranslations.h"]:
        lines = path.read_text(encoding="utf-8").split("\n")

        for number, line in enumerate(lines, 1):
            if re.search(r"TranslationEntries \w+ = \w+\(\);", line):
                found.append(f"{path.relative_to(ROOT)}:{number}")

    return found


def droppable_results() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*.h")):
            for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
                stripped = line.strip()

                if "nodiscard" in stripped or stripped.startswith("//"):
                    continue

                if re.match(r"^(?:static\s+|virtual\s+|inline\s+)*(?:(?:workpane::)?Result<|QFuture<(?:workpane::)?Result<)[^;]*\(", stripped):
                    found.append(f"{path.relative_to(ROOT)}:{number} answers with a result a caller may drop")

    return found


def miscounted_suite() -> list[str]:
    standard = (ROOT / "CLAUDE.md").read_text(encoding="utf-8")
    recorded = re.search(r"declares (\d+) independent CTest cases", standard)

    if recorded is None:
        return ["the standard records no suite size"]

    registered = 0

    for path in sorted((ROOT / "tests").rglob("*.cpp")):
        registered += len(re.findall(r"^TEST\(", path.read_text(encoding="utf-8"), re.M))

    if int(recorded.group(1)) != registered:
        return [f"the standard records {recorded.group(1)} cases and the suite registers {registered}"]

    return []


def undocumented_commands() -> list[str]:
    pages = [ROOT / "README.md"] + sorted((ROOT / "docs").glob("*.md"))
    documentation = "".join(page.read_text(encoding="utf-8") for page in pages if page.exists())

    if not documentation:
        return []

    declared = set(re.findall(r'"([a-z-]+)":\s*task_', (ROOT / "make.py").read_text(encoding="utf-8")))
    listed = set(re.findall(r"\|\s*\*\*([a-z-]+)\*\*\s*\|", documentation))
    return [f"{name} is a command the documentation never names" for name in sorted(declared - listed)]


def prose_opening_with_code() -> list[str]:
    found: list[str] = []

    for name in ("CLAUDE.md", "README.md"):
        path = ROOT / name
        if not path.exists():
            continue

        for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
            if re.match(r"^\s*(?:[-*]|\d+\.)\s+`", line):
                found.append(f"{name}:{number} opens its sentence with code")

    return found


def wrapped_statements() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            lines = path.read_text(encoding="utf-8").split("\n")
            protected = False

            for index, line in enumerate(lines[:-1]):
                stripped = line.strip()

                if stripped == "// clang-format off":
                    protected = True
                    continue

                if stripped == "// clang-format on":
                    protected = False
                    continue

                if protected or stripped.startswith("//") or stripped.startswith("#") or stripped.endswith("{"):
                    continue

                following = lines[index + 1].strip()

                # A table written on one line ends with the comma of its last entry, and the brace that closes it follows.
                if not re.search(r"(,|&&|\|\||==|!=)\s*$", stripped) or not following or following.startswith("}"):
                    continue

                found.append(f"{path.relative_to(ROOT)}:{index + 1}")

    return found


def crowded_scopes() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            lines = path.read_text(encoding="utf-8").split("\n")
            where = str(path.relative_to(ROOT))

            for index in range(len(lines) - 1):
                head = lines[index].strip()
                tail = lines[index + 1].strip()

                # A namespace, a class and an aggregate open a scope the project deliberately spaces out.
                opens_block = head.endswith("{") and not re.match(r"^(namespace|class|struct|enum|union|extern|template)\b", head) and not head.startswith("} ")

                if opens_block and not tail:
                    found.append(f"{where}:{index + 1} leaves a blank line after the brace that opens the scope")

                if not head and tail.startswith("}") and not tail.startswith("};") and "// namespace" not in tail:
                    found.append(f"{where}:{index + 2} leaves a blank line before the brace that closes the scope")

    return found


def repeated_comments() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            lines = path.read_text(encoding="utf-8").split("\n")

            for index in range(len(lines) - 1):
                first = lines[index].strip()
                second = lines[index + 1].strip()

                if not first.startswith("//") or not second.startswith("//"):
                    continue
                if first.startswith("// clang-format") or second.startswith("// clang-format"):
                    continue

                shared = difflib.SequenceMatcher(None, first, second).find_longest_match(0, len(first), 0, len(second))

                if shared.size >= 40:
                    found.append(f"{path.relative_to(ROOT)}:{index + 1} says {first[shared.a:shared.a + shared.size].strip()!r} twice")

    return found


def misgrouped_includes() -> list[str]:
    order = {"project": 1, "qt": 2, "platform": 3, "standard": 4}

    def kind(name: str) -> str:
        if name.startswith('"'):
            return "project"
        inner = name[1:-1]
        if inner.startswith("Q"):
            return "qt"
        if "/" in inner or inner.endswith(".h") or inner.endswith(".hpp"):
            return "platform"
        return "standard"

    found: list[str] = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            paragraphs: list[list[str]] = []
            current: list[str] = []

            for line in path.read_text(encoding="utf-8").split("\n"):
                match = re.match(r'\s*#include\s+([<"][^>"]+[>"])', line)
                if match:
                    current.append(match.group(1))
                    continue
                if current and (not line.strip() or not line.strip().startswith("#")):
                    paragraphs.append(current)
                    current = []

            if current:
                paragraphs.append(current)

            # The moc translation unit a class declared in a source file needs is generated and closes that file.
            paragraphs = [p for p in paragraphs if not (len(p) == 1 and p[0].endswith('.moc"'))]

            if not paragraphs:
                continue

            where = str(path.relative_to(ROOT))
            ranks: list[int] = []
            mixed = False

            for index, paragraph in enumerate(paragraphs):
                kinds = {kind(name) for name in paragraph}
                if index == 0 and path.suffix == ".cpp" and len(paragraph) == 1 and kinds == {"project"}:
                    ranks.append(0)
                    continue
                if len(kinds) > 1:
                    found.append(f"{where} puts {' and '.join(sorted(kinds))} headers in one group")
                    mixed = True
                    break
                ranks.append(order[kinds.pop()])

            if not mixed and ranks != sorted(ranks):
                found.append(f"{where} orders its include groups {ranks}")

    return found


def unused_declarations() -> list[str]:
    signals: dict[str, str] = {}
    values: dict[str, str] = {}

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*.h")):
            text = path.read_text(encoding="utf-8")
            where = str(path.relative_to(ROOT))

            for block in re.findall(r"signals:(.*?)(?:\n\s*(?:public|private|protected|};))", text, re.S):
                for name in re.findall(r"\bvoid\s+(\w+)\s*\(", block):
                    signals[name] = where

            for name, body in re.findall(r"enum\s+class\s+(\w+)[^{]*\{(.*?)\}", text, re.S):
                for value in re.findall(r"\b([A-Z]\w*)\s*(?:=[^,}]*)?\s*(?:,|$)", body):
                    values[f"{name}::{value}"] = where

    sources = ""

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix in (".cpp", ".h"):
                sources += path.read_text(encoding="utf-8", errors="ignore")

    found = []

    for name, where in sorted(signals.items()):
        if not re.search(r"::" + re.escape(name) + r"\b", sources) and not re.search(r"\bemit\s+" + re.escape(name) + r"\b", sources):
            found.append(f"the signal {name} in {where} is emitted by nothing and connected to nothing")

    for qualified, where in sorted(values.items()):
        if not re.search(r"\b" + re.escape(qualified) + r"\b", sources):
            found.append(f"the value {qualified} in {where} is named by nothing")

    return found


def mismatched_theme_tokens() -> list[str]:
    theme = (ROOT / "src" / "ui" / "Theme.cpp").read_text(encoding="utf-8")
    block = re.search(r"const QVector<QPair<QString, QString>> tokens\{(.*?)\n    \};", theme, re.S)

    if block is None:
        raise RuntimeError("The theme token substitution could not be read")

    declared = set(re.findall(r'QStringLiteral\("(@\w+)"\)', block.group(1)))
    written: set[str] = set()

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*.cpp")):
            if path.name == "Theme.cpp":
                continue
            written.update(re.findall(r"(@[a-zA-Z][a-zA-Z0-9]*)", path.read_text(encoding="utf-8", errors="ignore")))

    # A style sheet writes the unit against the token, so a token is consumed when it opens one of the names that were written.
    consumed = {name for name in declared if any(token.startswith(name) for token in written)}
    substituted = {token for token in written if any(token.startswith(name) for name in declared)}
    found = [f"{name} is substituted and no style sheet consumes it" for name in sorted(declared - consumed)]
    return found + [f"{token} is written and nothing substitutes it" for token in sorted(written - substituted)]


def unreachable_translations() -> list[str]:
    catalogs = sorted(ROOT.glob("plugins/*/*Translations.h")) + [ROOT / "src" / "plugins" / "CoreTranslations.h"]
    declared: dict[str, str] = {}

    for path in catalogs:
        for key in re.findall(r'QStringLiteral\("([a-z0-9-]+\.[a-z0-9-]+\.[a-z0-9-]+)"\)', path.read_text(encoding="utf-8")):
            declared[key] = str(path.relative_to(ROOT))

    sources = ""

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h", ".json") or path.name.endswith("Translations.h"):
                continue
            sources += path.read_text(encoding="utf-8", errors="ignore")

    # A family the code concatenates is written as a literal ending in its dot, and a key it formats carries the placeholder standing for the part that varies.
    concatenated = set(re.findall(r'"([a-z0-9-]+\.[a-z0-9-]+\.)"', sources))
    formatted = [re.compile("^" + re.escape(literal).replace("%1", "[a-z0-9-]+") + "$") for literal in set(re.findall(r'"([a-z0-9-]+\.[a-z0-9-]+\.[a-z0-9%-]*%1[a-z0-9-]*)"', sources))]
    unreachable = []

    for key, owner in sorted(declared.items()):
        if key in sources or key[: key.rfind(".") + 1] in concatenated:
            continue
        if any(pattern.match(key) for pattern in formatted):
            continue

        unreachable.append(f"{key} in {owner}")

    return unreachable


def namespace_scope_functions() -> list[str]:
    found = []
    declaration = re.compile(r"^(?:\[\[nodiscard\]\]\s*)?(?:inline\s+)?[A-Za-z_][\w:<>,\s\*&]*?\b(\w+)\s*\([^;]*\)\s*(?:const\s*)?(?:;|\{)$")

    for name in source_files():
        path = Path(name)

        if path.suffix != ".h":
            continue

        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            stripped = line.strip()

            if line.startswith(" ") or line.startswith("\t") or stripped.startswith("//"):
                continue

            match = declaration.match(stripped)

            qualified = f"::{match.group(1)}" in stripped if match is not None else False

            if match is not None and not qualified and match.group(1) not in ("if", "while", "for", "return", "switch"):
                found.append(f"{path.relative_to(ROOT)}:{number} declares {match.group(1)}")

    return found


def uncalled_methods() -> list[str]:
    # A declaration names a return type before the method, which is what tells it from a call written as a statement of its own.
    # A method that overrides another is called by whoever declared it, so only a declaration this project owns is read here.
    # A method written inline in the header is read the same way, because a body beside the class is as reachable as a prototype above it.
    declaration = re.compile(r"^\s{4,}(?:\[\[nodiscard\]\]\s*)?(?:static\s+|virtual\s+|explicit\s+)*(?!return\b|emit\b|throw\b|delete\b|co_return\b|case\b|if\b|for\b|while\b)[\w:<>,]+[\s\*&]+(\w+)\s*\([^;{]*\)\s*(?:const\s*)?(?:;|\{)\s*$")
    declared: dict[str, str] = {}

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*.h")):
            for line in path.read_text(encoding="utf-8").splitlines():
                match = declaration.match(line)

                if match is not None:
                    declared.setdefault(match.group(1), str(path.relative_to(ROOT)))

    sources = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix in (".cpp", ".h"):
                sources.append(path.read_text(encoding="utf-8", errors="ignore").splitlines())

    found = []

    for name, where in sorted(declared.items()):
        definition = re.compile(r"^[\w\[\]][\w:<>,\s\*&\[\]]*?\b\w+::" + re.escape(name) + r"\s*\(")
        mention = re.compile(r"\b" + re.escape(name) + r"\b")
        callers = 0

        for lines in sources:
            for line in lines:
                if mention.search(line) is None or declaration.match(line) is not None or definition.match(line) is not None:
                    continue
                callers += 1

        if callers == 0:
            found.append(f"{where} declares {name}")

    return found


def anonymous_namespaces() -> list[str]:
    found = []

    for name in source_files():
        for number, line in enumerate(Path(name).read_text(encoding="utf-8").splitlines(), start=1):
            if re.match(r"^\s*namespace\s*\{", line):
                found.append(f"{name}:{number}")

    return found


CATCH_ALL_NAMESPACES = ("utils", "util", "common", "helpers", "misc", "shared", "core")


def catch_all_namespaces() -> list[str]:
    found = []
    pattern = re.compile(r"^\s*namespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{")

    for name in source_files():
        for number, line in enumerate(Path(name).read_text(encoding="utf-8").splitlines(), start=1):
            match = pattern.match(line)
            if match is None:
                continue
            for component in match.group(1).split("::"):
                if component in CATCH_ALL_NAMESPACES:
                    found.append(f"{name}:{number}: {match.group(1)}")
                    break

    return found


PROCESS_ENDING_CONSTRUCTS = (
    (r"\bQ_UNREACHABLE\w*\s*\(", "an unreachable marker"),
    (r"\bQ_ASSERT\w*\s*\(", "an assertion"),
    (r"(?<!static_)\bassert\s*\(", "an assertion"),
    (r"\bstd::unreachable\s*\(", "an unreachable marker"),
    (r"\bstd::terminate\s*\(", "a termination"),
    (r"\bstd::abort\s*\(|(?<![\w:.>])abort\s*\(\s*\)", "a termination"),
    (r"\bqFatal\s*\(", "a termination"),
    (r"\bthrow\s+[^;]", "a thrown exception"),
)


def process_ending_constructs() -> list[str]:
    found = []
    patterns = [(re.compile(expression), reason) for expression, reason in PROCESS_ENDING_CONSTRUCTS]
    extensions = {".c", ".cc", ".cpp", ".h", ".hpp"}

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in extensions:
                continue

            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
                code = TEXT_LITERAL_PATTERN.sub('""', line.split("//", 1)[0])

                for pattern, reason in patterns:
                    if pattern.search(code):
                        found.append(f"{path.relative_to(ROOT)}:{number}: {reason}")
                        break

    return found


# A condition variable is always waited on with its lock and a predicate, so a wait carrying no second argument is a thread being joined.
BLOCKING_WAITS = re.compile(r"\b(waitForStarted|waitForFinished|waitForReadyRead|waitForBytesWritten|waitForDone|BlockingQueuedConnection)\b|\.wait\s*\(\s*[^,()]*\s*\)")
FUNCTION_DEFINITION = re.compile(r"^\s*(?!(?:if|for|while|switch|catch|return|else|do|case)\b)[A-Za-z_~][\w:<>,\s&*~]*?([A-Za-z_]\w*)\s*\([^;]*$")
TEARDOWN_FUNCTIONS = ("shutdown", "unloadPlugins", "drain", "drainTransports")


def interactive_blocking_waits() -> list[str]:
    found = []
    extensions = {".cpp", ".h"}

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in extensions:
                continue

            enclosing = ""
            destroying = False

            for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
                code = TEXT_LITERAL_PATTERN.sub('""', line.split("//", 1)[0])
                definition = FUNCTION_DEFINITION.match(code)

                # A definition closes its parameter list on its own line, while a call carrying a lambda leaves it open.
                if definition is not None and code.count("(") == code.count(")"):
                    enclosing = definition.group(1)
                    destroying = "~" in code.split("(", 1)[0]

                if BLOCKING_WAITS.search(code) and not destroying and enclosing not in TEARDOWN_FUNCTIONS:
                    found.append(f"{path.relative_to(ROOT)}:{number}: inside {enclosing or 'a free function'}")

    return found


WAIT_PREDICATE = re.compile(r"\b(\w+)\.wait(?:_for)?\s*\([^;]*?\[[^\]]*\]\s*\(\s*\)\s*\{(.*?)\}", re.S)
CONDITION_NOTIFY = re.compile(r"\b(\w+)\.notify_(?:one|all)\s*\(\s*\)")
GUARDED_MEMBER_WRITE = re.compile(r"\b(m_\w+)\s*(?:=[^=]|\.store\s*\(|\.exchange\s*\()")
HELD_LOCK = re.compile(r"\b(?:lock_guard|unique_lock|QMutexLocker|QWriteLocker|QReadLocker)\b")


def announced_without_its_lock() -> list[str]:
    found = []

    for directory in ("src", "plugins"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix != ".cpp":
                continue

            text = path.read_text(encoding="utf-8")
            watched: dict[str, set[str]] = {}

            for condition, predicate in WAIT_PREDICATE.findall(text):
                watched.setdefault(condition, set()).update(re.findall(r"\bm_\w+", predicate))

            if not watched:
                continue

            lines = text.splitlines()

            for number, line in enumerate(lines):
                notify = CONDITION_NOTIFY.search(line)

                if notify is None or notify.group(1) not in watched:
                    continue

                for offset in range(1, 7):
                    previous = number - offset

                    if previous < 0 or lines[previous].strip() in ("}", "};"):
                        break

                    write = GUARDED_MEMBER_WRITE.search(lines[previous])

                    if write is None or write.group(1) not in watched[notify.group(1)]:
                        continue

                    if not any(HELD_LOCK.search(lines[back]) for back in range(max(0, previous - 4), previous + 1)):
                        found.append(f"{path.relative_to(ROOT)}:{previous + 1}: {write.group(1)} announced to {notify.group(1)} without its lock")

    return found


def call_arguments(text: str, opened: int) -> list[str]:
    depth = 0
    index = opened - 1

    while index < len(text):
        depth += 1 if text[index] == "(" else -1 if text[index] == ")" else 0
        if depth == 0:
            break
        index += 1

    arguments: list[str] = []
    level = 0
    current = ""

    for character in text[opened:index]:
        level += 1 if character in "([{" else -1 if character in ")]}" else 0

        if character == "," and level == 0:
            arguments.append(current)
            current = ""
        else:
            current += character

    arguments.append(current)
    return arguments


def contextless_deferred_work() -> list[str]:
    # A connection names its context third and a single shot timer names it second.
    forms = ((re.compile(r"\bconnect\s*\("), 2), (re.compile(r"\bsingleShot\s*\("), 1))
    found = []

    for name in source_files():
        if not name.endswith((".cpp", ".h")):
            continue

        text = Path(name).read_text(encoding="utf-8")

        for pattern, position in forms:
            for call in pattern.finditer(text):
                arguments = call_arguments(text, call.end())

                if len(arguments) <= position:
                    continue

                receiver = arguments[position].strip()

                # A lambda capturing nothing reaches nothing, so it needs no context exactly as a continuation does.
                if receiver.startswith("[") and not receiver.startswith("[]"):
                    found.append(f"{name}:{text[:call.start()].count(chr(10)) + 1}")

    return sorted(found)


def dereferenced_senders() -> list[str]:
    found = []

    for name in source_files():
        if not name.endswith((".cpp", ".h")):
            continue

        for number, line in enumerate(Path(name).read_text(encoding="utf-8").splitlines(), start=1):
            if "sender()->" in line:
                found.append(f"{name}:{number}")

    return found


def divergent_backend_conditions() -> list[str]:
    backends = (
        ROOT / "src" / "terminal" / "platform" / "posix" / "PosixPtyBackend.cpp",
        ROOT / "src" / "terminal" / "platform" / "windows" / "ConPtyBackend.cpp",
    )
    spoken: dict[str, set[tuple[str, str]]] = {}
    named: dict[str, set[tuple[str, str]]] = {}

    for path in backends:
        text = path.read_text(encoding="utf-8")
        found = re.findall(r'"(terminal_[a-z_]+)",\s*"([^"]+)"', text)
        found += re.findall(r'QStringLiteral\("(terminal_[a-z_]+)"\),\s*QStringLiteral\("([^"]+)"\)', text)

        for code, message in found:
            spoken.setdefault(code, set()).add((path.name, message))
            named.setdefault(message, set()).add((path.name, code))

    divergent = []

    for code, said in sorted(spoken.items()):
        if len({message for _, message in said}) > 1:
            divergent.append(code + " is " + " and ".join(f"{message!r} in {name}" for name, message in sorted(said)))

    # One sentence carried by two codes is the same condition spelled differently, which the code side alone cannot show.
    for message, said in sorted(named.items()):
        if len({code for _, code in said}) > 1:
            divergent.append(repr(message) + " is " + " and ".join(f"{code} in {name}" for name, code in sorted(said)))

    return divergent


def stray_comments() -> list[str]:
    found: list[str] = []

    for directory in ("src", "plugins", "tests"):
        for path in sorted((ROOT / directory).rglob("*")):
            if path.suffix not in (".cpp", ".h"):
                continue

            lines = path.read_text(encoding="utf-8").split("\n")

            for number, line in enumerate(lines, 1):
                stripped = line.strip()

                if not stripped.startswith("//") or stripped.startswith("// clang-format"):
                    continue

                body = stripped[2:].strip()

                if not body:
                    continue

                where = f"{path.relative_to(ROOT)}:{number}"

                if number < len(lines) and not lines[number].strip():
                    found.append(f"{where} explains nothing, because a blank line follows it")

                if ";" in body[:-1]:
                    found.append(f"{where} divides a sentence with a semicolon")

                if not body.endswith("."):
                    found.append(f"{where} does not end its sentence")

                if body[0].isalpha() and not body[0].isupper():
                    found.append(f"{where} opens its sentence in lower case")

    return found


def task_audit(_: Context) -> None:
    unguarded = unprotected_lambdas()

    if unguarded:
        raise RuntimeError("Every lambda is formatted by hand, so these need clang-format markers:\n  " + "\n  ".join(unguarded))

    uncalled = uncalled_methods()

    if uncalled:
        raise RuntimeError("A method nobody calls is one nobody removed, so every declaration is reached from somewhere:\n  " + "\n  ".join(uncalled))

    stray = stray_comments()

    if stray:
        raise RuntimeError("Every comment is a complete sentence sitting on what it explains:\n  " + "\n  ".join(stray))

    mismatched = mismatched_translations()

    if mismatched:
        raise RuntimeError("A sentence is given exactly the arguments it declares, because one it never asked for reaches the reader as a warning:\n  " + "\n  ".join(mismatched))

    unguarded = unguarded_continuations()

    if unguarded:
        raise RuntimeError("A continuation that reaches anything is given the object it reaches as its context, so destroying that object cancels it:\n  " + "\n  ".join(unguarded))

    unlisted = unlisted_icons()

    if unlisted:
        raise RuntimeError("The accessor answers the complete icon set, so the cases that render and compare every icon reach these too:\n  " + "\n  ".join(unlisted))

    inherited = inherited_catalogs()

    if inherited:
        raise RuntimeError("Every language declares the keys it spells, because a catalog built from another one cannot be told from one that forgot a sentence:\n  " + "\n  ".join(inherited))

    droppable = droppable_results()

    if droppable:
        raise RuntimeError("A failure a caller may drop is a failure nobody reports, so every result is declared nodiscard:\n  " + "\n  ".join(droppable))

    miscounted = miscounted_suite()

    if miscounted:
        raise RuntimeError("The standard records what the suite really is, because a number nobody keeps is a number nobody believes:\n  " + "\n  ".join(miscounted))

    undocumented = undocumented_commands()

    if undocumented:
        raise RuntimeError("The README lists the commands this repository answers, because a reader looks for them there:\n  " + "\n  ".join(undocumented))

    opening = prose_opening_with_code()

    if opening:
        raise RuntimeError("A sentence never begins with code or a path, so a line always opens with a word:\n  " + "\n  ".join(opening))

    wrapped = wrapped_statements()

    if wrapped:
        raise RuntimeError("Every call, declaration and return stays complete on one physical line, because the formatter never wraps one:\n  " + "\n  ".join(wrapped))

    crowded = crowded_scopes()

    if crowded:
        raise RuntimeError("A scope begins and ends at its brace, so no blank line sits against either one:\n  " + "\n  ".join(crowded))

    repeated = repeated_comments()

    if repeated:
        raise RuntimeError("Two comments that say the same clause are one comment, because the second explains nothing the first did not:\n  " + "\n  ".join(repeated))

    misgrouped = misgrouped_includes()

    if misgrouped:
        raise RuntimeError("Includes are one group for the header of the file, one for project headers, one for Qt, one for the platform and one for the standard library, in that order:\n  " + "\n  ".join(misgrouped))

    unused = unused_declarations()

    if unused:
        raise RuntimeError("A declaration nothing reaches is one the reader never meets, so it is removed rather than kept:\n  " + "\n  ".join(unused))

    mismatched = mismatched_theme_tokens()

    if mismatched:
        raise RuntimeError("Every theme value a style sheet writes is substituted and every token substituted is written, because either half alone reaches the screen as itself:\n  " + "\n  ".join(mismatched))

    unreachable = unreachable_translations()

    if unreachable:
        raise RuntimeError("A sentence nothing reaches is one the reader never sees, so every key is named by the code or composed from a family it names:\n  " + "\n  ".join(unreachable))

    ending = process_ending_constructs()

    if ending:
        raise RuntimeError("Nothing in the product ends the process, because a state the code could have handled must never cost the reader the application:\n  " + "\n  ".join(ending))

    announced = announced_without_its_lock()

    if announced:
        raise RuntimeError("A value a waiting thread reads under a lock is changed under that same lock, because a change only announced beside it is a notification nobody was waiting for yet:\n  " + "\n  ".join(announced))

    blocking = interactive_blocking_waits()

    if blocking:
        raise RuntimeError("A blocking wait belongs to final teardown alone, because the thread that draws must never wait on storage, a process, a worker or the network:\n  " + "\n  ".join(blocking))

    loose = namespace_scope_functions()

    if loose:
        raise RuntimeError("Every function is a member of a class, because a declaration at namespace scope has no owner:\n  " + "\n  ".join(loose))

    anonymous = anonymous_namespaces()

    if anonymous:
        raise RuntimeError("Every constant, type and function belongs to a named namespace, because nothing is hidden from the other files:\n  " + "\n  ".join(anonymous))

    catch_all = catch_all_namespaces()

    if catch_all:
        raise RuntimeError("A namespace is named for the context it belongs to, because a name that says nothing is where unrelated modules end up mixed together:\n  " + "\n  ".join(catch_all))

    contextless = contextless_deferred_work()

    if contextless:
        raise RuntimeError("A connection or a timer whose lambda captures anything is given the object it reaches as its context, so destroying that object cancels it:\n  " + "\n  ".join(contextless))

    dereferenced = dereferenced_senders()

    if dereferenced:
        raise RuntimeError("A slot reaches its sender through a cast it checks, because a slot called directly answers with nothing:\n  " + "\n  ".join(dereferenced))

    divergent = divergent_backend_conditions()

    if divergent:
        raise RuntimeError("Two implementations of one backend report a shared condition by one name, because only one of them compiles per platform:\n  " + "\n  ".join(divergent))


# Cppcheck reads what the audits already read, so the audits run first and their findings are the ones a reader acts on.
def task_lint(context: Context) -> None:
    task_audit(context)
    run([
        executable("cppcheck"),
        "--enable=warning,performance,portability",
        "--std=c++20",
        "--suppress=missingIncludeSystem",
        "--suppress=unknownMacro",
        "--error-exitcode=1",
        str(ROOT / "src"),
        str(ROOT / "plugins"),
    ])


def task_sanitize(context: Context) -> None:
    sanitize_context = Context("Debug", ROOT / "build" / "sanitize", context.jobs, context.verbose)
    cmake_configure(
        sanitize_context,
        "-DWORKPANE_BUILD_TESTS=ON",
        "-DWORKPANE_ENABLE_SANITIZERS=ON",
    )
    cmake_build(sanitize_context)
    # An instrumented case costs several times the machine of a plain one and some of them start an instrumented child, so the suite runs on half the cores.
    run([
        executable("ctest"),
        "--test-dir",
        str(sanitize_context.build_dir),
        "--parallel",
        str(max(1, sanitize_context.jobs // 2)),
        "--output-on-failure",
        "--no-tests=error",
    ])


def task_package(context: Context) -> None:
    package_context = Context("Release", ROOT / "build" / "release", context.jobs, context.verbose)
    cmake_configure(package_context, "-DWORKPANE_BUILD_TESTS=OFF")
    cmake_build(package_context, "package")


VERSION_PATTERN = re.compile(r"^(project\(Workpane VERSION )(\d+\.\d+\.\d+)( LANGUAGES .*\)$)", re.MULTILINE)


def cmake_setting(name: str) -> str:
    match = re.search(rf'^set\({name} "([^"]+)"\)$', (ROOT / "cmake" / "Version.cmake").read_text(encoding="utf-8"), re.MULTILINE)
    if match is None:
        raise RuntimeError(f"The {name} declaration was not found in cmake/Version.cmake")
    return match.group(1)


def current_version() -> str:
    match = VERSION_PATTERN.search((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if match is None:
        raise RuntimeError("The project version declaration was not found in CMakeLists.txt")
    return match.group(2)


def task_models(_: Context, value: str | None = None) -> None:
    if not value:
        raise RuntimeError("the models task needs the path of a LiteLLM checkout")
    run([sys.executable, str(ROOT / "scripts" / "import_models.py"), value])


def task_version(_: Context, value: str | None = None) -> None:
    if value is None:
        print(current_version())
        return

    if re.fullmatch(r"\d+\.\d+\.\d+", value) is None:
        raise RuntimeError(f"The version must use the MAJOR.MINOR.PATCH format: {value}")

    path = ROOT / "CMakeLists.txt"
    path.write_text(VERSION_PATTERN.sub(rf"\g<1>{value}\g<3>", path.read_text(encoding="utf-8")), encoding="utf-8")
    print(f"{current_version()}")


BUNDLED_PLUGIN_COUNT = 8
HARDWARE_COMPONENT_COUNT = 8


def macos_staged_bundle(build_dir: Path) -> Path:
    staging = build_dir / "_CPack_Packages" / "Darwin" / "DragNDrop" / f"Workpane-{current_version()}-Darwin"
    return staging / "Workpane.app"


def validate_macos_bundle(bundle: Path) -> None:
    run([executable("codesign"), "--verify", "--deep", "--strict", str(bundle)])

    plugins = sorted((bundle / "Contents" / "PlugIns").glob("libworkpane-*.dylib"))
    if len(plugins) != BUNDLED_PLUGIN_COUNT:
        raise RuntimeError(f"The bundle contains {len(plugins)} plugins instead of {BUNDLED_PLUGIN_COUNT}")

    helper = bundle / "Contents" / "Frameworks" / "QtWebEngineCore.framework" / "Helpers" / "QtWebEngineProcess.app"
    if not helper.exists():
        raise RuntimeError("The bundle does not contain the Qt WebEngine helper process")

    linkage = subprocess.run([executable("otool"), "-L", str(bundle / "Contents" / "MacOS" / "Workpane")], check=True, capture_output=True, text=True).stdout
    if "QtCore.framework" not in linkage:
        raise RuntimeError("The application executable does not link Qt 6 dynamically")


def windows_staged_tree(build_dir: Path) -> Path:
    return build_dir / "_CPack_Packages" / "win64" / "NSIS" / f"Workpane-{current_version()}-win64"


def validate_installed_tree(root: Path, binary_dir: Path) -> None:
    executables = [name for name in ("Workpane.exe", "Workpane") if (binary_dir / name).exists()]
    if not executables:
        # A package laid out somewhere else is the common failure, so the report names what it really carries.
        carried = sorted(str(entry.relative_to(root)) for entry in root.rglob("Workpane*"))
        raise RuntimeError(f"The package holds no application executable in {binary_dir}, and carries {carried or sorted(entry.name for entry in root.iterdir())}")

    # The application looks for its own plugins beside the executable, so a package that puts them anywhere else cannot start.
    directory = binary_dir / "plugins"
    plugins = sorted(directory.glob("workpane-*.dll")) + sorted(directory.glob("libworkpane-*.so"))
    if len(plugins) != BUNDLED_PLUGIN_COUNT:
        raise RuntimeError(f"The package holds {len(plugins)} plugins in {directory} instead of {BUNDLED_PLUGIN_COUNT}")

    helpers = list(root.rglob("QtWebEngineProcess*"))
    if not helpers:
        raise RuntimeError("The package does not contain the Qt WebEngine helper process")

    libraries = list(root.rglob("Qt6Core.dll")) + list(root.rglob("libQt6Core.so*"))
    if not libraries:
        raise RuntimeError("The package does not ship Qt 6 as a shared library")

    components = list(root.rglob("hwinfo_*.dll")) + list(root.rglob("libhwinfo_*.so"))
    if len(components) != HARDWARE_COMPONENT_COUNT:
        raise RuntimeError(f"The package holds {len(components)} hardware components instead of {HARDWARE_COMPONENT_COUNT}")


def validate_debian_package(package: Path) -> None:
    with tempfile.TemporaryDirectory() as staging:
        contents = subprocess.run([executable("dpkg-deb"), "--fsys-tarfile", str(package)], check=True, capture_output=True).stdout
        with tarfile.open(fileobj=io.BytesIO(contents)) as archive:
            archive.extractall(staging, filter="tar")

        root = Path(staging)
        validate_installed_tree(root, root / "opt" / "workpane" / "bin")

        entry = root / "usr" / "share" / "applications" / "workpane.desktop"
        if not entry.is_file():
            raise RuntimeError("The package installs no desktop entry, so the application never appears in a launcher")

        icon = root / "usr" / "share" / "icons" / "hicolor" / "512x512" / "apps" / "workpane.png"
        if not icon.is_file():
            raise RuntimeError("The package installs no application icon for its desktop entry")

        # The command a shell already looks for is what makes the application reachable without naming the directory it lives in.
        command = root / "usr" / "bin" / "workpane"
        if not command.is_symlink() or os.readlink(command) != "/opt/workpane/bin/Workpane":
            raise RuntimeError("The package does not link the application into the command search path")


def task_validate_package(context: Context) -> None:
    package_context = Context("Release", ROOT / "build" / "release", context.jobs, context.verbose)
    if sys.platform == "darwin":
        validate_macos_bundle(macos_staged_bundle(package_context.build_dir))
    elif os.name == "nt":
        installers = sorted(package_context.build_dir.glob("Workpane-*.exe"))
        if not installers:
            raise RuntimeError("No Workpane installer was produced")
        # The installer carries the tree it was compressed from, so validation reads that tree exactly as it reads the assembled macOS bundle.
        staged = windows_staged_tree(package_context.build_dir)
        validate_installed_tree(staged, staged / "bin")
    else:
        packages = sorted(package_context.build_dir.glob("workpane_*.deb"))
        if not packages:
            raise RuntimeError("No Workpane Debian package was produced")
        validate_debian_package(packages[-1])
    print("Package validation succeeded")


def application_data_dir() -> Path:
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / cmake_setting("WORKPANE_ORGANIZATION_NAME") / cmake_setting("WORKPANE_PRODUCT_NAME")
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA")
        if not base:
            raise RuntimeError("LOCALAPPDATA is not defined")
        return Path(base) / cmake_setting("WORKPANE_ORGANIZATION_NAME") / cmake_setting("WORKPANE_PRODUCT_NAME")
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / cmake_setting("WORKPANE_ORGANIZATION_NAME") / cmake_setting("WORKPANE_PRODUCT_NAME")


def task_reset_data(context: Context) -> None:
    directory = application_data_dir()
    if not directory.exists():
        print(f"No application data was found at {directory}")
        return

    print(f"This permanently removes the Workpane database and every plugin state in {directory}")
    if context.value != "force" and input("Type the word remove to continue: ").strip() != "remove":
        print("The application data was preserved")
        return

    shutil.rmtree(directory)
    print(f"Removed {directory}")


def task_clean(context: Context) -> None:
    if (context.build_dir / "CMakeCache.txt").exists():
        cmake_build(context, "clean")


def task_distclean(_: Context) -> None:
    build_root = ROOT / "build"
    if build_root.exists():
        shutil.rmtree(build_root)


def task_all(context: Context) -> None:
    task_format_check(context)
    task_audit(context)
    task_build(context)
    task_test(context)


TASKS = {
    "all": task_all,
    "build": task_build,
    "clean": task_clean,
    "configure": task_configure,
    "coverage": task_coverage,
    "distclean": task_distclean,
    "doctor": task_doctor,
    "format": task_format,
    "format-check": task_format_check,
    "audit": task_audit,
    "lint": task_lint,
    "package": task_package,
    "reset-data": task_reset_data,
    "run": task_run,
    "sanitize": task_sanitize,
    "test": task_test,
    "validate-package": task_validate_package,
    "models": task_models,
    "version": task_version,
}

TASK_DESCRIPTIONS = {
    "all": "Run formatting checks, build and tests",
    "build": "Build the application",
    "clean": "Clean the selected build directory",
    "configure": "Configure the selected build directory",
    "coverage": "Generate the coverage reports",
    "distclean": "Remove every build directory",
    "doctor": "Check required and optional development tools",
    "format": "Format all C and C++ source files",
    "format-check": "Validate C and C++ source formatting",
    "audit": "Run the audits this project declares for itself",
    "lint": "Run those audits and then Cppcheck against production sources",
    "package": "Create the release package",
    "reset-data": "Remove the application database and every plugin state",
    "run": "Build and run the application",
    "sanitize": "Build and run with sanitizers enabled",
    "test": "Build and run registered tests",
    "validate-package": "Validate the assembled release package",
    "models": "Rebuild the AI model catalog from a LiteLLM checkout given as the value",
    "version": "Print the application version or set a new MAJOR.MINOR.PATCH value",
}


def print_tasks() -> None:
    width = max(len(name) for name in TASKS)
    print("Available tasks:\n")
    for name in sorted(TASKS):
        print(f"  {name:<{width}}  {TASK_DESCRIPTIONS[name]}")


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Workpane development tasks")
    parser.add_argument("task", choices=sorted(TASKS), nargs="?")
    parser.add_argument("value", nargs="?")
    parser.add_argument("--configuration", choices=["Debug", "Release", "RelWithDebInfo"], default="Debug")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    if arguments.task is None:
        print_tasks()
        return 0

    build_dir = arguments.build_dir or ROOT / "build" / arguments.configuration.lower()
    context = Context(arguments.configuration, build_dir.resolve(), arguments.jobs, arguments.verbose, arguments.value)
    try:
        if arguments.task == "version":
            task_version(context, arguments.value)
        elif arguments.task == "models":
            task_models(context, arguments.value)
        else:
            TASKS[arguments.task](context)
    except (RuntimeError, subprocess.CalledProcessError) as error:
        print(f"\nTask failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
