#!/usr/bin/env python3

from __future__ import annotations

import argparse
import io
import os
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent
# The floor is what the suite really reaches, so the task refuses a fall rather than an unmet ambition.
COVERAGE_LINE_FLOOR = 70
BUNDLED_PLUGINS = tuple(sorted(path.name for path in (ROOT / "plugins").iterdir() if path.is_dir()))
LUCIDE_FONT = ROOT / "assets" / "fonts" / "Lucide.ttf"
LUCIDE_GLYPHS = ROOT / "src" / "ui" / "LucideGlyphs.cpp"


@dataclass(frozen=True)
class Context:
    configuration: str
    build_dir: Path
    jobs: int
    verbose: bool
    value: str | None = None


def run(command: list[str], *, cwd: Path = ROOT, env: dict[str, str] | None = None) -> None:
    # A command over every source file names each of them, so only its beginning is shown.
    shown = command if len(command) <= 12 else [*command[:4], f"... and {len(command) - 4} more arguments"]
    print(f"\n> {' '.join(shown)}", flush=True)
    subprocess.run(command, cwd=cwd, env=env, check=True)


def executable(name: str) -> str:
    resolved = shutil.which(name)

    if resolved is None:
        raise RuntimeError(f"The required executable \"{name}\" was not found")

    return resolved


# The job count reaches the builds a dependency runs by itself, such as the one of OpenSSL, through the variable CMake reads for it.
def build_environment(context: Context) -> dict[str, str]:
    return {**os.environ, "CMAKE_BUILD_PARALLEL_LEVEL": str(context.jobs)}


def cmake_configure(context: Context, *definitions: str) -> None:
    run([
        executable("cmake"),
        "-S",
        str(ROOT),
        "-B",
        str(context.build_dir),
        "-G",
        "Ninja",
        f"-DCMAKE_BUILD_TYPE={context.configuration}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        *definitions,
    ], env=build_environment(context))


def cmake_build(context: Context, *targets: str) -> None:
    command = [executable("cmake"), "--build", str(context.build_dir), "--parallel", str(context.jobs)]

    if targets:
        command.extend(["--target", *targets])

    if context.verbose:
        command.append("--verbose")

    run(command, env=build_environment(context))


def ctest(context: Context, jobs: int) -> None:
    command = [executable("ctest"), "--test-dir", str(context.build_dir), "--parallel", str(jobs), "--output-on-failure", "--no-tests=error"]

    if context.verbose:
        command.append("--verbose")

    run(command)


def task_doctor(_: Context) -> None:
    for name in ("cmake", "ninja", "clang-format"):
        print(f"The tool \"{name}\" is at \"{executable(name)}\"")

    for name in ("cppcheck", "gcovr", "docker"):
        print(f"The tool \"{name}\" is at \"{shutil.which(name)}\"" if shutil.which(name) else f"The tool \"{name}\" is not installed")


def task_configure(context: Context) -> None:
    cmake_configure(context)


def task_build(context: Context) -> None:
    if not (context.build_dir / "CMakeCache.txt").exists():
        task_configure(context)

    cmake_build(context)


def app_path(context: Context) -> Path:
    if sys.platform == "darwin":
        return context.build_dir / "bin" / "Workpane.app" / "Contents" / "MacOS" / "Workpane"

    return context.build_dir / "bin" / ("Workpane.exe" if os.name == "nt" else "Workpane")


def task_run(context: Context) -> None:
    task_build(context)
    arguments = ["--data-dir", str(Path(context.value).resolve())] if context.value else []
    run([str(app_path(context)), *arguments])


def task_test(context: Context) -> None:
    cmake_configure(context, "-DWORKPANE_BUILD_TESTS=ON")
    cmake_build(context)
    ctest(context, context.jobs)


def task_coverage(context: Context) -> None:
    coverage = Context("Debug", ROOT / "build" / "coverage", context.jobs, context.verbose)
    gcovr = executable("gcovr")
    cmake_configure(coverage, "-DWORKPANE_BUILD_TESTS=ON", "-DWORKPANE_ENABLE_COVERAGE=ON")
    cmake_build(coverage)

    # The counters of a previous run belong to the sources as they were then, so a report merged with them describes neither build.
    for stale in coverage.build_dir.rglob("*.gcda"):
        stale.unlink()

    ctest(coverage, coverage.jobs)
    report = coverage.build_dir / "coverage"
    report.mkdir(parents=True, exist_ok=True)
    compiler = ""

    for line in (coverage.build_dir / "CMakeCache.txt").read_text(encoding="utf-8").split("\n"):
        if line.startswith("CMAKE_CXX_COMPILER_ID:"):
            compiler = line.split("=", 1)[1]

    # A clang toolchain writes the same data through a tool of its own, so gcovr is told which one reads it.
    reader = ["--gcov-executable", "llvm-cov gcov"] if "Clang" in compiler else []
    run([gcovr, *reader, "--root", str(ROOT), "--filter", str(ROOT / "src"), "--exclude-unreachable-branches", "--html-details", str(report / "index.html"), "--xml", str(report / "cobertura.xml"), "--fail-under-line", str(COVERAGE_LINE_FLOOR), str(coverage.build_dir)])


def source_files(*directories: str) -> list[Path]:
    extensions = {".cpp", ".h", ".mm"}
    return [path for directory in directories if (ROOT / directory).exists() for path in sorted((ROOT / directory).rglob("*")) if path.suffix in extensions]


def lua_files() -> list[Path]:
    return [path for directory in ("lua", "plugins") for path in sorted((ROOT / directory).rglob("*.lua"))]


# The glyph names of the bundled Lucide face with the private code point each icon is drawn at, read from its post and cmap tables and sorted by name.
def lucide_glyphs() -> list[tuple[str, int]]:
    data = LUCIDE_FONT.read_bytes()
    tables = {}

    for index in range(struct.unpack(">H", data[4:6])[0]):
        tag, _, offset, length = struct.unpack(">4sIII", data[12 + 16 * index:28 + 16 * index])
        tables[tag.decode("latin-1")] = (offset, length)

    post, post_length = tables["post"]

    if struct.unpack(">I", data[post:post + 4])[0] != 0x20000:
        raise RuntimeError("The Lucide face no longer names its glyphs in a version 2 post table")

    count = struct.unpack(">H", data[post + 32:post + 34])[0]
    indexes = struct.unpack(f">{count}H", data[post + 34:post + 34 + 2 * count])
    cursor = post + 34 + 2 * count
    strings = []

    while cursor < post + post_length:
        length = data[cursor]
        strings.append(data[cursor + 1:cursor + 1 + length].decode("latin-1"))
        cursor += 1 + length

    names = [strings[index - 258] if index >= 258 else "" for index in indexes]
    glyphs = {}
    cmap = tables["cmap"][0]

    for subtable in range(struct.unpack(">H", data[cmap + 2:cmap + 4])[0]):
        start = cmap + struct.unpack(">I", data[cmap + 8 + 8 * subtable:cmap + 12 + 8 * subtable])[0]

        if struct.unpack(">H", data[start:start + 2])[0] != 4:
            continue

        segments = struct.unpack(">H", data[start + 6:start + 8])[0] // 2
        ends = struct.unpack(f">{segments}H", data[start + 14:start + 14 + 2 * segments])
        starts = struct.unpack(f">{segments}H", data[start + 16 + 2 * segments:start + 16 + 4 * segments])
        deltas = struct.unpack(f">{segments}h", data[start + 16 + 4 * segments:start + 16 + 6 * segments])
        ranges = start + 16 + 6 * segments
        offsets = struct.unpack(f">{segments}H", data[ranges:ranges + 2 * segments])

        for segment in range(segments):
            for code in range(starts[segment], ends[segment] + 1):
                if code == 0xFFFF:
                    continue

                glyph = (code + deltas[segment]) & 0xFFFF

                if offsets[segment] != 0:
                    address = ranges + 2 * segment + offsets[segment] + 2 * (code - starts[segment])
                    glyph = struct.unpack(">H", data[address:address + 2])[0]
                    glyph = (glyph + deltas[segment]) & 0xFFFF if glyph else 0

                if 0xE000 <= code <= 0xF8FF and 0 < glyph < len(names) and names[glyph]:
                    glyphs.setdefault(names[glyph], code)

    return sorted(glyphs.items())


def task_icons(_: Context) -> None:
    entries = ", ".join(f'{{"{name}", 0x{code:04X}}}' for name, code in lucide_glyphs())
    LUCIDE_GLYPHS.write_text(f"""#include "ui/LucideGlyphs.h"

#include <algorithm>

namespace workpane::ui {{

// The table is written by the icons task of the task runner from the glyph names of the bundled Lucide face, sorted by name so a name is found by halving it.
const std::vector<LucideGlyphs::Entry>& LucideGlyphs::table() {{
    static const std::vector<Entry> entries{{{entries}}};
    return entries;
}}

std::optional<ImWchar> LucideGlyphs::find(std::string_view name) {{
    const auto& entries = table();
    const auto found = std::ranges::lower_bound(entries, name, {{}}, &Entry::name);

    if (found == entries.end() || found->name != name) {{
        return std::nullopt;
    }}

    return found->glyph;
}}

}} // namespace workpane::ui
""", encoding="utf-8")
    print(f"Wrote \"{relative(LUCIDE_GLYPHS)}\"")


# The table of glyph names the product carries is the one the bundled face answers, so a new face can never leave a name pointing at the wrong glyph.
def lucide_table_problems() -> list[str]:
    written = re.findall(r'\{"([a-z0-9-]+)", 0x([0-9A-F]{4})\}', LUCIDE_GLYPHS.read_text(encoding="utf-8")) if LUCIDE_GLYPHS.exists() else []
    expected = [(name, f"{code:04X}") for name, code in lucide_glyphs()]

    if written != expected:
        return [f"{relative(LUCIDE_GLYPHS)} differs from the glyph names of {relative(LUCIDE_FONT)}, so run the icons task"]

    return []


def task_format(_: Context) -> None:
    run([executable("clang-format"), "-i", *[str(path) for path in source_files("src", "tests")]])


def task_format_check(_: Context) -> None:
    run([executable("clang-format"), "--dry-run", "--Werror", *[str(path) for path in source_files("src", "tests")]])


def relative(path: Path) -> str:
    return str(path.relative_to(ROOT))


LAMBDA_PATTERN = re.compile(r"\[([^\]\[]*)\]\s*(\([^)]*\))?\s*(mutable\s*)?(->\s*[A-Za-z_:<>, ]+)?\s*\{")
CAPTURE_PATTERN = re.compile(r"[&=]?\s*[A-Za-z_&=, .*()]*")
TEXT_LITERAL_PATTERN = re.compile(r"\"(?:[^\"\\]|\\.)*\"|'(?:[^'\\]|\\.)*'")


def unprotected_lambdas() -> list[str]:
    found: list[str] = []

    for path in source_files("src", "tests"):
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
                if match.group(1) and not CAPTURE_PATTERN.fullmatch(match.group(1)):
                    continue

                found.append(f"{relative(path)}:{number}")

    return found


def comment_lines() -> list[tuple[Path, int, str, list[str]]]:
    found: list[tuple[Path, int, str, list[str]]] = []

    for path in source_files("src", "tests") + lua_files():
        marker = "--" if path.suffix == ".lua" else "//"
        lines = path.read_text(encoding="utf-8").split("\n")

        for number, line in enumerate(lines, 1):
            stripped = line.strip()

            if stripped.startswith(marker) and not stripped.startswith("// clang-format") and not stripped.startswith("--[[") and not stripped.startswith("---"):
                found.append((path, number, stripped[len(marker):].strip(), lines))

    return found


# Every comment is a complete sentence that opens in upper case, ends with a period, never divides itself with a semicolon and sits on what it explains.
def malformed_comments() -> list[str]:
    found: list[str] = []

    for path, number, body, lines in comment_lines():
        where = f"{relative(path)}:{number}"

        if not body:
            continue

        if number < len(lines) and not lines[number].strip():
            found.append(f"The line \"{where}\" explains nothing, because a blank line follows it")

        if ";" in body[:-1]:
            found.append(f"The line \"{where}\" divides a sentence with a semicolon")

        if not body.endswith("."):
            found.append(f"The line \"{where}\" does not end its sentence")

        if body[0].isalpha() and not body[0].isupper():
            found.append(f"The line \"{where}\" opens its sentence in lower case")

    return found


# A header describes its types and never its methods, sections or members, whose comments belong beside their definitions.
def member_comments_in_headers() -> list[str]:
    found: list[str] = []
    declaration = re.compile(r"^(class|struct|enum|template\s*<)")

    for path in source_files("src", "tests"):
        if path.suffix != ".h":
            continue

        lines = path.read_text(encoding="utf-8").split("\n")

        for index, line in enumerate(lines):
            if not line.strip().startswith("//") or line.strip().startswith("// clang-format"):
                continue

            following = next((candidate for candidate in lines[index + 1:] if not candidate.strip().startswith("//")), "")

            if line.startswith(" ") or not declaration.match(following):
                found.append(f"{relative(path)}:{index + 1}")

    return found


CATCH_ALL_NAMESPACES = ("utils", "util", "common", "helpers", "misc", "shared", "core")


# A namespace is named for the area it belongs to, because a name that says nothing is where unrelated code ends up mixed together.
def catch_all_namespaces() -> list[str]:
    found: list[str] = []
    pattern = re.compile(r"^\s*namespace\s+([A-Za-z_][A-Za-z0-9_:]*)\s*\{")

    for path in source_files("src", "tests"):
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = pattern.match(line)

            if match is not None and any(component in CATCH_ALL_NAMESPACES for component in match.group(1).split("::")):
                found.append(f"The line \"{relative(path)}:{number}\" opens the namespace \"{match.group(1)}\"")

    return found


# An operator left at the end of a line is a statement the formatter never wrote, because it never wraps one.
CONTINUED_STATEMENT = re.compile(r"(?:,|&&|\|\||==|!=|<<|>>|->|\?|[-+*/%]=|(?<![-+*/%<>=!&|])[-+*/%]|(?<![-+*/%<>=!&|^])=)\s*$")


def wrapped_statements() -> list[str]:
    found: list[str] = []

    for path in source_files("src", "tests"):
        lines = path.read_text(encoding="utf-8").split("\n")
        protected = False
        raw = False

        for index, line in enumerate(lines[:-1]):
            stripped = line.strip()

            # The text of a raw string literal is another language, such as Lua in a test, and is never a statement of this one.
            if raw or ('R"(' in line and ')"' not in line.split('R"(', 1)[1]):
                raw = ')"' not in line or not raw
                continue

            if stripped == "// clang-format off":
                protected = True
                continue

            if stripped == "// clang-format on":
                protected = False
                continue

            if protected or stripped.startswith(("//", "#")) or stripped.endswith("{"):
                continue

            following = lines[index + 1].strip()

            # A table written on one line ends with the comma of its last entry, and the brace that closes it follows.
            if CONTINUED_STATEMENT.search(stripped) and following and not following.startswith("}"):
                found.append(f"{relative(path)}:{index + 1}")

    return found


# A scope begins and ends at its brace, so no blank line sits against either one.
def crowded_scopes() -> list[str]:
    found: list[str] = []

    for path in source_files("src", "tests"):
        lines = path.read_text(encoding="utf-8").split("\n")

        for index in range(len(lines) - 1):
            head = lines[index].strip()
            tail = lines[index + 1].strip()
            opens = head.endswith("{") and not re.match(r"^(namespace|class|struct|enum|union|extern|template)\b", head) and not head.startswith("} ")

            if opens and not tail:
                found.append(f"The line \"{relative(path)}:{index + 1}\" leaves a blank line after the brace that opens the scope")

            if not head and tail.startswith("}") and not tail.startswith("};") and "// namespace" not in tail:
                found.append(f"The line \"{relative(path)}:{index + 2}\" leaves a blank line before the brace that closes the scope")

    return found


# A block is separated by a blank line from the statements around it, so conditions, loops and returns never read as one glued mass.
# A comment above a block belongs to the block, so the blank line sits above the comment, and a lambda or an initializer that closes counts as a closed block.
def glued_blocks() -> list[str]:
    found: list[str] = []
    continuations = ("}", "else", "case ", "default:", "#", "break;", "private:", "public:", "protected:")

    for path in source_files("src", "tests"):
        lines = path.read_text(encoding="utf-8").split("\n")

        for index in range(len(lines) - 1):
            line = lines[index]
            closed = line.strip() == "}" or re.match(r"^\}\)*;$", line.strip()) is not None
            after = index + 1

            while after < len(lines) and lines[after].strip().startswith("// clang-format"):
                after += 1

            following = lines[after] if after < len(lines) else ""
            continues = following.strip() == "" or following.strip().startswith(continuations)

            if closed and following.strip() and indentation(following) == indentation(line) and not continues:
                found.append(f"The line \"{relative(path)}:{after + 1}\" follows a closed block without a blank line")

        for index, line in enumerate(lines):
            if re.match(r"(?:if|for|while|switch|do) ?[({]", line.strip()) is None:
                continue

            above = index - 1

            while above >= 0 and lines[above].strip().startswith("//") and not lines[above].strip().startswith("// clang-format"):
                above -= 1

            previous = lines[above].strip() if above >= 0 else ""
            statement = previous and not previous.endswith(("{", ":")) and not previous.startswith(("//", "}", "#"))

            if statement and indentation(lines[above]) == indentation(line):
                found.append(f"The line \"{relative(path)}:{index + 1}\" opens a block right after a statement")

    return found


CPP_FUNCTION = re.compile(r"\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:final\s*)?(?:mutable\s*)?(?:->\s*[\w:<>, *&]+\s*)?\{$")
CPP_CONTROL = re.compile(r"^(?:if|for|while|switch|else|do|catch|try)\b|^\} else\b")
CPP_DECLARATION = re.compile(r"^(?:const|constexpr|static|auto|using)\b|^(?:typename\s+)?[A-Za-z_][\w:]*(?:<.*>)?(?:\s*[*&]+\s*|\s+)[A-Za-z_]\w*\s*(?:=|\(|\{|;)")


# Answers the lines of every function body that stand at the outermost level of that body, which is where the method reads as a beginning, a middle and an end.
def function_bodies(lines: list[str], opens, closes) -> list[tuple[int, int, int]]:
    bodies: list[tuple[int, int, int]] = []

    for index, line in enumerate(lines):
        if not opens(line):
            continue

        close = next((candidate for candidate in range(index + 1, len(lines)) if lines[candidate].strip() and indentation(lines[candidate]) <= indentation(line) and closes(lines[candidate].strip())), None)

        if close is not None:
            bodies.append((index, close, indentation(line) + 4))

    return bodies


# Answers whether a return at the outermost level of a function body stands right after mutations or side effects, since it may only follow the declarations it reads.
def glued_return(lines: list[str], start: int, index: int, comment: str, declaration) -> bool:
    above = index - 1

    while above > start and lines[above].strip().startswith(comment):
        above -= 1

    if above <= start or not lines[above].strip() or lines[above].startswith("#"):
        return False

    while above > start and lines[above].strip() and not lines[above].startswith("#"):
        text = lines[above].strip()

        if not text.startswith(comment) and (indentation(lines[above]) != indentation(lines[index]) or not declaration(text)):
            return True

        above -= 1

    return False


# The return that ends a function body is separated by a blank line from the mutations and side effects before it, so the method reads as a beginning, a middle and an end.
def closing_returns() -> list[str]:
    found: list[str] = []
    opens = lambda line: CPP_FUNCTION.search(line.strip()) is not None and CPP_CONTROL.match(line.strip()) is None
    closes = lambda text: text.startswith("}")
    declaration = lambda text: CPP_DECLARATION.match(text) is not None and not text.startswith(("return", "delete"))

    for path in source_files("src", "tests"):
        lines = path.read_text(encoding="utf-8").split("\n")
        cleared = [line if not line.strip().startswith("// clang-format") else "" for line in lines]

        for start, close, body in function_bodies(lines, opens, closes):
            for index in range(start + 1, close):
                if indentation(lines[index]) == body and re.match(r"return\b", lines[index].strip()) and glued_return(cleared, start, index, "//", declaration):
                    found.append(f"The line \"{relative(path)}:{index + 1}\" returns right after the statements it follows")

    return found


TYPE_DEFINITION = re.compile(r"^(?:template <[^>]*> )?(?:class|struct) ([A-Za-z_]\w*)(?: final)?(?: : [^{;]+)? ?\{", re.MULTILINE)
MEMBER_DEFINITION = re.compile(r"^(?!return\b)[\w\[\]][\w\[\]<>:,\s\*&]*?\b([A-Z]\w*)::(?:[A-Z]\w*::)?~?\w+\s*\(", re.MULTILINE)


# A header declares one class named after the file, and a source defines the members of that one class and nothing else.
def classes_per_file() -> list[str]:
    found: list[str] = []

    for path in source_files("src", "tests"):
        text = path.read_text(encoding="utf-8")
        types = TYPE_DEFINITION.findall(text)
        named = ", ".join(f'"{name}"' for name in types)

        if path.suffix == ".h" and len(types) > 1:
            found.append(f"The file \"{relative(path)}\" declares {named}")

        if path.suffix == ".h" and len(types) == 1 and types[0] != path.stem:
            found.append(f"The file \"{relative(path)}\" declares \"{types[0]}\" under another name")

        if path.suffix in (".cpp", ".mm") and path.is_relative_to(ROOT / "src") and types:
            found.append(f"The file \"{relative(path)}\" defines the class {named} outside a header")

        if path.suffix in (".cpp", ".mm") and path.is_relative_to(ROOT / "tests") and len(types) > 1:
            found.append(f"The file \"{relative(path)}\" defines {named}")

        owners = sorted(set(MEMBER_DEFINITION.findall(text)) - {path.stem})
        owned = ", ".join(f'"{name}"' for name in owners)

        if path.suffix in (".cpp", ".mm") and path.is_relative_to(ROOT / "src") and owners:
            found.append(f"The file \"{relative(path)}\" defines members of {owned}")

    return found


# Every source of the product is listed by the build, so a file nobody compiles cannot hide in the tree.
def unlisted_sources() -> list[str]:
    listed = (ROOT / "src" / "CMakeLists.txt").read_text(encoding="utf-8") + (ROOT / "cmake" / "Platform.cmake").read_text(encoding="utf-8")
    return [relative(path) for path in source_files("src") if path.name != "main.cpp" and str(path.relative_to(ROOT / "src")) not in listed]


# A method is recognized by the return type before its name, and one that overrides another is called by whoever declared it.
METHOD_DECLARATION = re.compile(r"^\s{4,}(?:\[\[nodiscard\]\]\s*)?(?:static\s+|virtual\s+|explicit\s+)*(?!return\b|throw\b|delete\b|case\b|if\b|for\b|while\b)[\w:<>,]+[\s\*&]+(\w+)\s*\([^;{]*\)\s*(?:const\s*)?(?:=\s*0\s*)?(?:;|\{)\s*$")


# A method nobody calls is one nobody removed, so every method a header declares is reached from somewhere.
def uncalled_methods() -> list[str]:
    declared: dict[str, str] = {}

    for path in source_files("src"):
        if path.suffix != ".h":
            continue

        for line in path.read_text(encoding="utf-8").splitlines():
            match = METHOD_DECLARATION.match(line)

            if match is not None and "override" not in line:
                declared.setdefault(match.group(1), relative(path))

    sources = [path.read_text(encoding="utf-8", errors="ignore").splitlines() for path in source_files("src", "tests")]
    found: list[str] = []

    for name, where in sorted(declared.items()):
        definition = re.compile(r"^[\w\[\]][\w:<>,\s\*&\[\]]*?\b\w+::" + re.escape(name) + r"\s*\(")
        mention = re.compile(r"\b" + re.escape(name) + r"\b")

        if not any(mention.search(line) and not METHOD_DECLARATION.match(line) and not definition.match(line) for lines in sources for line in lines):
            found.append(f"The line \"{where}\" declares \"{name}\"")

    return found


def anonymous_namespaces() -> list[str]:
    return [f"{relative(path)}:{number}" for path in source_files("src", "tests") for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1) if re.match(r"^\s*namespace\s*\{", line)]


# A function or a constant always belongs to a class, so nothing is declared at namespace scope where no owner explains it.
def namespace_scope_declarations() -> list[str]:
    found: list[str] = []
    function = re.compile(r"^(?:\[\[nodiscard\]\]\s*)?(?:inline\s+|static\s+)?[A-Za-z_][\w:<>,\s\*&]*?\b(\w+)\s*\([^;]*\)\s*(?:const\s*)?(?:;|\{)$")
    constant = re.compile(r"^(?:inline\s+|static\s+)*(?:constexpr|const)\s+[\w:<>,\s\*&]+?\b(\w+)\s*(?:=|\{)")
    tests = ("TEST", "TEST_F", "TEST_P")

    for path in source_files("src", "tests"):
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            stripped = line.strip()

            if line.startswith((" ", "\t")) or stripped.startswith("//"):
                continue

            match = constant.match(stripped)

            # The definition of a static member outside its class is qualified by the class, which is the owner the rule asks for.
            if match is not None and f"::{match.group(1)}" not in stripped:
                found.append(f"The line \"{relative(path)}:{number}\" declares the constant \"{match.group(1)}\"")
                continue

            if path.suffix != ".h" and not stripped.endswith("{"):
                continue

            match = function.match(stripped)

            if match is not None and f"::{match.group(1)}" not in stripped and match.group(1) not in ("if", "while", "for", "return", "switch", "main", *tests):
                found.append(f"The line \"{relative(path)}:{number}\" declares \"{match.group(1)}\"")

    return found


# A class is named for what it is, so no helper, utility or manager of loose functions stands in for a real owner.
def helper_classes() -> list[str]:
    utility = re.compile(r"^\s*(?:class|struct)\s+(\w*(?:Helpers|Util|Utils|Utilities))\b", re.M)
    helper = re.compile(r"^(?:class|struct) (\w+Helper) final \{(.*?)^\};", re.M | re.S)
    member = re.compile(r"^\s+(?!(?:\[\[\w+\]\]\s+)?(?:template\s*<[^>]*>\s+)?static\b|public:|private:|protected:|//|\}|enum\b|struct\b|using\b)\S.*", re.M)
    found: list[str] = []

    for path in source_files("src", "tests"):
        text = path.read_text(encoding="utf-8")
        found.extend(f"The file \"{relative(path)}\" declares \"{name}\", which is named \"<Subject>Helper\" instead" for name in utility.findall(text))

        for name, body in helper.findall(text):
            if member.search(body):
                found.append(f"The helper \"{name}\" in \"{relative(path)}\" holds a member that is not static")

    return found


# Every declaration of the product lives inside the workpane namespace, and only the entry point stands outside it.
def files_outside_namespace() -> list[str]:
    return [relative(path) for path in source_files("src", "tests") if path.name != "main.cpp" and not re.search(r"^namespace workpane(?:::\w+)* \{$", path.read_text(encoding="utf-8"), re.M)]


# Includes are one group for the header of the file, then project headers, then third party headers and then the standard library.
def misgrouped_includes() -> list[str]:
    order = {"project": 1, "third-party": 2, "standard": 3}
    found: list[str] = []

    def kind(name: str) -> str:
        if name.startswith('"'):
            return "project"

        inner = name[1:-1]
        return "third-party" if "/" in inner or inner.endswith(".h") or inner.endswith(".hpp") else "standard"

    for path in source_files("src", "tests"):
        paragraphs: list[list[str]] = []
        branched: list[bool] = []
        current: list[str] = []
        directive = False

        for line in path.read_text(encoding="utf-8").split("\n"):
            match = re.match(r'\s*#include\s+([<"][^>"]+[>"])', line)

            if match:
                if not current:
                    branched.append(directive)
                    directive = False

                current.append(match.group(1))
                continue
            # Anything but another include ends a group, so the headers of each platform branch are a group of their own.
            if current:
                paragraphs.append(current)
                current = []

            directive = directive or line.strip().startswith("#")

        if current:
            paragraphs.append(current)

        ranks: list[int] = []
        previous = ""

        for index, paragraph in enumerate(paragraphs):
            kinds = {kind(name) for name in paragraph}

            if index == 0 and path.suffix in (".cpp", ".mm") and len(paragraph) == 1 and kinds == {"project"}:
                continue

            if len(kinds) > 1:
                found.append(f"The file \"{relative(path)}\" puts {' and '.join(sorted(kinds))} headers in one group")
                break

            if paragraph != sorted(paragraph):
                found.append(f"The file \"{relative(path)}\" does not sort the group that starts with \"{paragraph[0]}\"")

            # Two groups of project headers apart only by a blank line are one group split in two, while the headers of a platform branch stand apart and system headers keep the order they need.
            group = next(iter(kinds))

            if group == "project" and group == previous and not branched[index]:
                found.append(f"The file \"{relative(path)}\" splits its {group} headers into two groups before \"{paragraph[0]}\"")

            previous = group
            ranks.append(order[group])

        if ranks != sorted(ranks):
            found.append(f"The file \"{relative(path)}\" orders its include groups {ranks}")

    return found


def else_after_leaving() -> list[str]:
    found: list[str] = []

    for path in source_files("src", "tests"):
        lines = path.read_text(encoding="utf-8").split("\n")

        for index, line in enumerate(lines):
            if not re.match(r"^\s*\}\s*else\b", line):
                continue

            previous = next((candidate.strip() for candidate in reversed(lines[:index]) if candidate.strip()), "")

            if re.match(r"^(return\b|continue;|break;)", previous):
                found.append(f"{relative(path)}:{index + 1}")

    return found


LUA_BLOCK = re.compile(r"^(?:if|for|while|repeat)\b|^(?:local\s+)?function\b")
LUA_OPENS = re.compile(r"(?:\bthen|\bdo|\belse|\brepeat|\bfunction\b.*\)|[({,])$")
LUA_CLOSES = re.compile(r"^(?:(?:end|else|elseif|until)\b|[})])")


def indentation(line: str) -> int:
    return len(line) - len(line.lstrip())


# A Lua block is separated by a blank line from the statements around it, as in C++, so conditions, loops and functions never read as one glued mass.
# A comment above a block belongs to the block, and a function passed as an argument that closes counts as a closed block.
def lua_glued_blocks() -> list[str]:
    found: list[str] = []

    for path in lua_files():
        lines = path.read_text(encoding="utf-8").split("\n")

        for index in range(len(lines) - 1):
            line = lines[index].rstrip()
            following = lines[index + 1].rstrip()
            same = following.strip() and indentation(following) == indentation(line)
            closed = line.strip() == "end" or re.match(r"^end\s*[)}\]][\s)}\]]*$", line.strip()) is not None

            if closed and same and not LUA_CLOSES.match(following.strip()):
                found.append(f"The line \"{relative(path)}:{index + 2}\" follows a closed block without a blank line")

        for index, line in enumerate(lines):
            if not LUA_BLOCK.match(line.strip()):
                continue

            above = index - 1

            while above >= 0 and lines[above].strip().startswith("--"):
                above -= 1

            previous = lines[above].strip() if above >= 0 else ""
            statement = previous and not LUA_OPENS.search(previous) and not LUA_CLOSES.match(previous)

            if statement and indentation(lines[above]) == indentation(line):
                found.append(f"The line \"{relative(path)}:{index + 1}\" opens a block right after a statement")

    return found


# The return that ends a Lua function is separated by a blank line from the mutations and side effects before it, as in C++.
def lua_closing_returns() -> list[str]:
    found: list[str] = []
    opens = lambda line: re.search(r"\bfunction\b[^()]*\([^()]*\)$", line.strip()) is not None and not line.strip().startswith("--")
    closes = lambda text: re.match(r"^end\b", text) is not None
    declaration = lambda text: re.match(r"^local\s+(?!function\b)", text) is not None

    for path in lua_files():
        lines = path.read_text(encoding="utf-8").split("\n")

        for start, close, body in function_bodies(lines, opens, closes):
            for index in range(start + 1, close):
                if indentation(lines[index]) == body and re.match(r"return\b", lines[index].strip()) and glued_return(lines, start, index, "--", declaration):
                    found.append(f"The line \"{relative(path)}:{index + 1}\" returns right after the statements it follows")

    return found


# A Lua scope begins and ends at the words that open and close it, so no blank line sits against either one.
def lua_crowded_scopes() -> list[str]:
    found: list[str] = []

    for path in lua_files():
        lines = path.read_text(encoding="utf-8").split("\n")

        for index in range(len(lines) - 1):
            head = lines[index].strip()
            tail = lines[index + 1].strip()

            if head and not head.startswith("--") and LUA_OPENS.search(head) and not head.endswith(",") and not tail:
                found.append(f"The line \"{relative(path)}:{index + 1}\" leaves a blank line after the word that opens the scope")

            if not head and index > 0 and LUA_CLOSES.match(tail):
                found.append(f"The line \"{relative(path)}:{index + 2}\" leaves a blank line before the word that closes the scope")

    return found


# A Lua branch that returns, breaks or raises is never followed by an else, so the rest of the function reads unindented.
def lua_else_after_leaving() -> list[str]:
    found: list[str] = []

    for path in lua_files():
        lines = path.read_text(encoding="utf-8").split("\n")

        for index, line in enumerate(lines):
            if not re.match(r"^\s*(?:else|elseif)\b", line):
                continue

            previous = next((candidate.strip() for candidate in reversed(lines[:index]) if candidate.strip()), "")

            if re.match(r"^(?:return\b|break$|error\()", previous):
                found.append(f"{relative(path)}:{index + 1}")

    return found


# A Lua module answers a table and adds nothing to the global environment, so every name it defines is local or a field of that table.
def lua_module_problems() -> list[str]:
    found: list[str] = []
    assignment = re.compile(r"^([A-Za-z_]\w*)\s*=[^=]")
    function = re.compile(r"^function\s+([A-Za-z_]\w*)\s*\(")

    for path in lua_files():
        lines = path.read_text(encoding="utf-8").split("\n")
        statements = [line for line in lines if line.strip() and not line.startswith((" ", "--", "}", ")", "end"))]

        declared = {name.strip() for line in lines for names in re.findall(r"^local\s+(?:function\s+)?([\w\s,]+?)(?:\s*=|\s*\(|$)", line) for name in names.split(",")}

        for number, line in enumerate(lines, 1):
            match = assignment.match(line) or function.match(line)

            if match is not None and match.group(1) not in declared:
                found.append(f"The line \"{relative(path)}:{number}\" adds \"{match.group(1)}\" to the global environment")

        answered = re.match(r"^return ([A-Za-z_]\w*)$", statements[-1]) if statements else None

        if statements and not statements[-1].startswith("return ") and path.name != "bootstrap.lua":
            found.append(f"The file \"{relative(path)}\" answers no table")

        if answered is not None and answered.group(1) not in declared:
            found.append(f"The file \"{relative(path)}\" answers \"{answered.group(1)}\", which it never declares")

    return found


PYTHON_BLOCK = re.compile(r"^(?:if|for|while|with|try|def|class)\b")
PYTHON_CONTINUES = ("else", "elif", "except", "finally", ")", "]", "}", "#", "@")


# The task runner follows the rule of the product, so a Python block is separated by a blank line from the statements around it as well.
def python_glued_blocks() -> list[str]:
    found: list[str] = []
    lines = (ROOT / "make.py").read_text(encoding="utf-8").split("\n")

    for index in range(len(lines) - 1):
        line = lines[index].rstrip()
        following = lines[index + 1].rstrip()

        if not line.strip() or not following.strip() or line.strip().startswith("@"):
            continue

        above = index

        while above > 0 and lines[above].strip().startswith("#"):
            above -= 1

        previous = lines[above].rstrip()
        opens = PYTHON_BLOCK.match(following.strip()) is not None and previous.strip() and not previous.strip().startswith(("#", "@")) and indentation(following) == indentation(previous) and not previous.endswith((":", "(", "[", "{", ","))
        closes = indentation(following) < indentation(line) and not line.strip().startswith("#") and not following.strip().startswith(PYTHON_CONTINUES) and not line.endswith((",", "(", "[", "{"))

        if opens:
            found.append(f"The line \"make.py:{index + 2}\" opens a block right after a statement")

        if closes:
            found.append(f"The line \"make.py:{index + 2}\" follows a closed block without a blank line")

    return found


# The return that ends a function of the task runner is separated by a blank line from the calls before it, as in the product.
def python_closing_returns() -> list[str]:
    found: list[str] = []
    lines = (ROOT / "make.py").read_text(encoding="utf-8").split("\n")
    opens = lambda line: re.match(r"^\s*def\b.*:$", line) is not None
    closes = lambda text: not text.startswith(("#", ")", "]", "}"))
    declaration = lambda text: re.match(r"^\w+(?:\s*:\s*[^=]+)?\s*=[^=]", text) is not None

    for start, close, body in function_bodies(lines, opens, closes):
        for index in range(start + 1, close):
            if indentation(lines[index]) == body and re.match(r"return\b", lines[index].strip()) and glued_return(lines, start, index, "#", declaration):
                found.append(f"The line \"make.py:{index + 1}\" returns right after the statements it follows")

    return found


def plugin_catalog(path: Path) -> dict[str, dict[str, str]]:
    text = path.read_text(encoding="utf-8")
    catalogs: dict[str, dict[str, str]] = {}

    for language, body in re.findall(r"^\s{4}(\w+) = \{\n(.*?)^\s{4}\},", text, re.MULTILINE | re.DOTALL):
        catalogs[language] = dict(re.findall(r'\["([^"]+)"\] = "((?:[^"\\]|\\.)*)"', body))

    return catalogs


def placeholders(text: str) -> int:
    return max([int(number) for number in re.findall(r"%([1-9])", text)] or [0])


# Every plugin spells every key in every language with the same arguments, names only keys of its own and reaches every key it declares.
def plugin_catalog_problems() -> list[str]:
    found: list[str] = []
    grammar = re.compile(r"[a-z0-9][a-z0-9-]*\.[a-z0-9][a-z0-9-]*\.[a-z0-9][a-z0-9-]*")

    for directory in sorted(path for path in (ROOT / "plugins").iterdir() if path.is_dir()):
        plugin = directory.name
        catalog_path = directory / "translations.lua"

        if not (directory / "plugin.lua").exists() or not catalog_path.exists():
            found.append(f"The plugin \"plugins/{plugin}\" carries no pair of \"plugin.lua\" and \"translations.lua\"")
            continue

        catalogs = plugin_catalog(catalog_path)
        english = catalogs.get("en", {})

        if set(catalogs) != {"en", "pt"}:
            found.append(f"The plugin \"plugins/{plugin}\" declares the languages {sorted(catalogs)} instead of \"en\" and \"pt\"")

        for language, entries in catalogs.items():
            for key in sorted(set(english) ^ set(entries)):
                found.append(f"The plugin \"plugins/{plugin}\" spells \"{key}\" in only one of \"en\" and \"{language}\"")

            for key, text in entries.items():
                if not grammar.fullmatch(key) or not key.startswith(plugin + "."):
                    found.append(f"The plugin \"plugins/{plugin}\" declares the key \"{key}\" outside its own grammar")

                if key in english and placeholders(text) != placeholders(english[key]):
                    found.append(f"The plugin \"plugins/{plugin}\" gives \"{language}:{key}\" other arguments than English")

        # A key composed in code is reached through a family prefix such as a tone name, or through the owner prefix joined to a suffix such as a title.
        sources = "\n".join(path.read_text(encoding="utf-8") for path in sorted(directory.rglob("*.lua")) if path != catalog_path)
        literals = set(re.findall(r'"([^"\n]*)"', sources))
        families = [literal for literal in literals if literal.startswith(plugin + ".") and literal.count(".") == 2 and literal not in english]
        suffixes = [literal for literal in literals if literal.startswith(".") and literal.count(".") == 1] if plugin + "." in literals else []

        for key in sorted(english):
            composed = any(key.startswith(family) for family in families) or any(key.endswith(suffix) for suffix in suffixes)

            if key not in literals and not composed:
                found.append(f"The plugin \"plugins/{plugin}\" declares \"{key}\", which nothing reaches")

    return found


# A sentence of the documentation opens with a word and never divides itself with a semicolon, which code blocks and tables are free of.
def prose_problems() -> list[str]:
    found: list[str] = []
    documents = [ROOT / "README.md", ROOT / "AGENTS.md", ROOT / "PLAN.md", ROOT / "MISSING.md", *sorted((ROOT / "docs").glob("*.md")), *sorted((ROOT / "plugins").glob("*/assets/templates/*.md"))]

    for path in documents:
        if not path.exists():
            continue

        fenced = False

        for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
            if line.lstrip().startswith("```"):
                fenced = not fenced
                continue

            if fenced or line.lstrip().startswith(("|", "<")):
                continue

            if re.match(r"^\s*(?:[-*]|\d+\.)?\s*(?:\[[ x]\]\s*)?`", line):
                found.append(f"The line \"{relative(path)}:{number}\" opens its sentence with code")

            if ";" in re.sub(r"`[^`]*`", "", line):
                found.append(f"The line \"{relative(path)}:{number}\" divides a sentence with a semicolon")

    return found


UNMARKED_CODE = re.compile(r"(?<![\w`\"'<%/.:-])(?:--[a-z][\w-]*|[a-z]+_\w+|[a-z]+[A-Z]\w*|[a-z_]\w*(?:\.[a-z_]\w*)+|\w+::\w+|\w+\(\)|[\w.-]+/[\w./*-]*\w)(?![\w`\"'(])")
MARKED_TEXT = re.compile(r"`[^`]*`|\\\"(?:[^\"\\]|\\[^\"])*\\\"|\"[^\"]*\"|https?://\S+|\]\([^)]*\)|<[^>]*>|\{\{\w+\}\}")
PROPER_NAMES = {"macOS", "iOS", "iPadOS", "tvOS", "watchOS", "visionOS", "xAI", "e.g", "i.e"}


def unmarked_code(text: str) -> list[str]:
    return [token for token in UNMARKED_CODE.findall(MARKED_TEXT.sub(" ", text.replace("\\n", " "))) if token not in PROPER_NAMES]


# A command, an option, a path or an identifier inside a comment, a document or a translation is marked, so it never reads as part of the sentence around it.
def unmarked_reserved_words() -> list[str]:
    found: list[str] = []

    for path, number, body, _ in comment_lines():
        for token in unmarked_code(body):
            found.append(f"The line \"{relative(path)}:{number}\" names \"{token}\" without marking it")

    scripts = [ROOT / "make.py", ROOT / "CMakeLists.txt", ROOT / "tests" / "CMakeLists.txt", *sorted((ROOT / "cmake").rglob("*.cmake"))]

    for path in scripts:
        for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
            if not line.lstrip().startswith("# "):
                continue

            for token in unmarked_code(line.lstrip()[2:]):
                found.append(f"The line \"{relative(path)}:{number}\" names \"{token}\" without marking it")

    documents = [ROOT / "README.md", ROOT / "AGENTS.md", *sorted((ROOT / "docs").glob("*.md"))]

    for path in documents:
        fenced = False

        for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
            if line.lstrip().startswith("```"):
                fenced = not fenced
                continue

            if fenced or line.lstrip().startswith(("|", "<")):
                continue

            for token in unmarked_code(line):
                found.append(f"The line \"{relative(path)}:{number}\" names \"{token}\" without marking it")

    for path in sorted((ROOT / "plugins").glob("*/translations.lua")):
        for number, line in enumerate(path.read_text(encoding="utf-8").split("\n"), 1):
            value = re.match(r'\s*\["[^"]+"\] = "((?:[^"\\]|\\.)*)",?$', line)

            if value is None:
                continue

            for token in unmarked_code(value.group(1)):
                found.append(f"The line \"{relative(path)}:{number}\" names \"{token}\" without marking it")

    return found


# A declaration reaches the network only through an archive whose digest is written beside it, never through a branch, a tag or a bare address.
def unpinned_downloads() -> list[str]:
    found: list[str] = []
    digest = r"SHA256=(?:[0-9a-f]{64}|\$\{\w+\})"

    for path in [ROOT / "CMakeLists.txt", *sorted((ROOT / "cmake").rglob("*.cmake"))]:
        text = path.read_text(encoding="utf-8")

        for match in re.finditer(r"\b(FetchContent_Declare|file\(DOWNLOAD)\(?([^)]*)\)", text):
            number = text.count("\n", 0, match.start()) + 1
            kind, body = match.group(1), match.group(2)

            if kind == "FetchContent_Declare" and (re.search(r"\bGIT_REPOSITORY\b", body) or not re.search(r"\bURL_HASH\s+" + digest, body)):
                found.append(f"The line \"{relative(path)}:{number}\" declares a dependency without the digest of its archive")

            if kind == "file(DOWNLOAD" and not re.search(r"\bEXPECTED_HASH\s+" + digest, body):
                found.append(f"The line \"{relative(path)}:{number}\" downloads a file without its digest")

    return found


def undocumented_commands() -> list[str]:
    guide = ROOT / "docs" / "development.md"

    if not guide.exists():
        return ["The file \"docs/development.md\" is missing"]

    text = guide.read_text(encoding="utf-8")
    return [f"The guide \"docs/development.md\" does not document the task \"{name}\"" for name in sorted(TASKS) if f"make.py {name}" not in text]


AUDITS = (
    (unprotected_lambdas, "Every lambda is formatted by hand, so these need clang-format markers"),
    (malformed_comments, "Every comment is a complete sentence sitting on what it explains"),
    (member_comments_in_headers, "A header comments its types and never its methods, sections or members"),
    (anonymous_namespaces, "Every constant, type and function belongs to a named namespace"),
    (catch_all_namespaces, "A namespace is named for the area it belongs to"),
    (wrapped_statements, "Every call, declaration and statement stays complete on one physical line"),
    (crowded_scopes, "A scope begins and ends at its brace, so no blank line sits against either one"),
    (glued_blocks, "A block that closes is followed by a blank line, so conditions, loops and returns never read as one glued mass"),
    (closing_returns, "The return that ends a function follows its mutations and side effects after a blank line"),
    (uncalled_methods, "A method nobody calls is one nobody removed"),
    (classes_per_file, "Every file holds exactly one class named after it, and every function is a member of that class"),
    (unlisted_sources, "Every source of the product is listed by the build"),
    (namespace_scope_declarations, "Every function and constant is a member of a class, because a declaration at namespace scope has no owner"),
    (helper_classes, "A class that gathers stateless operations is named \"<Subject>Helper\" and holds only static members"),
    (files_outside_namespace, "Every file declares its contents inside the namespace \"workpane\""),
    (misgrouped_includes, "Includes are grouped as the header of the file, project headers, third party headers and the standard library, each sorted"),
    (else_after_leaving, "A branch that returns, continues or breaks is never followed by an else"),
    (lua_glued_blocks, "A Lua block is separated by a blank line from the statements around it"),
    (lua_closing_returns, "The return that ends a Lua function follows its mutations and side effects after a blank line"),
    (lua_crowded_scopes, "A Lua scope begins and ends at the words that open and close it, so no blank line sits against either one"),
    (lua_else_after_leaving, "A Lua branch that returns, breaks or raises is never followed by an else"),
    (lua_module_problems, "A Lua module answers a table and adds nothing to the global environment"),
    (python_glued_blocks, "The task runner separates each block from the statements around it"),
    (python_closing_returns, "The return that ends a function of the task runner follows its calls after a blank line"),
    (plugin_catalog_problems, "Every plugin catalog is complete, consistent and reachable in every language"),
    (lucide_table_problems, "The table of Lucide glyph names is the one the bundled face answers"),
    (unpinned_downloads, "Every dependency is fetched from an archive pinned by its digest"),
    (undocumented_commands, "The development guide lists the commands this repository answers"),
    (prose_problems, "A sentence of the documentation opens with a word and never divides itself with a semicolon"),
    (unmarked_reserved_words, "A command, an option, a path or an identifier inside a sentence is marked"),
)


def task_audit(_: Context) -> None:
    failures = []

    for audit, rule in AUDITS:
        found = audit()

        if found:
            failures.append(rule + ":\n  " + "\n  ".join(found))

    if failures:
        raise RuntimeError("\n\n".join(failures))

    print("Every audit passed")


# Cppcheck reads what the audits already read, so the audits run first and their findings are the ones a reader acts on.
def task_lint(context: Context) -> None:
    task_audit(context)
    run([executable("cppcheck"), "--enable=warning,performance,portability", "--std=c++20", "--suppress=missingIncludeSystem", "--suppress=unknownMacro", "--inline-suppr", "--error-exitcode=1", "-I", str(ROOT / "src"), str(ROOT / "src")])


def task_sanitize(context: Context) -> None:
    sanitize = Context("Debug", ROOT / "build" / "sanitize", context.jobs, context.verbose)
    cmake_configure(sanitize, "-DWORKPANE_BUILD_TESTS=ON", "-DWORKPANE_ENABLE_SANITIZERS=ON")
    cmake_build(sanitize)
    # An instrumented case costs several times the machine of a plain one, so the suite runs on half the cores.
    ctest(sanitize, max(1, sanitize.jobs // 2))


def release_context(context: Context) -> Context:
    return Context("Release", ROOT / "build" / "release", context.jobs, context.verbose)


def task_package(context: Context) -> None:
    package = release_context(context)
    cmake_configure(package, "-DWORKPANE_BUILD_TESTS=OFF")
    cmake_build(package, "package")


VERSION_PATTERN = re.compile(r"^(project\(Workpane VERSION )(\d+\.\d+\.\d+)( LANGUAGES .*\)$)", re.MULTILINE)


def current_version() -> str:
    match = VERSION_PATTERN.search((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))

    if match is None:
        raise RuntimeError("The project version declaration was not found in \"CMakeLists.txt\"")

    return match.group(2)


def task_version(context: Context) -> None:
    if context.value is None:
        print(current_version())
        return

    if re.fullmatch(r"\d+\.\d+\.\d+", context.value) is None:
        raise RuntimeError(f"The version \"{context.value}\" must use the \"MAJOR.MINOR.PATCH\" format")

    path = ROOT / "CMakeLists.txt"
    path.write_text(VERSION_PATTERN.sub(rf"\g<1>{context.value}\g<3>", path.read_text(encoding="utf-8")), encoding="utf-8")
    print(current_version())


# The resources are what the product reads at startup, so a package missing any of them opens nothing.
def validate_resources(resources: Path) -> None:
    required = [resources / "lua" / "workpane" / "bootstrap.lua", resources / "fonts" / "Inter-Regular.ttf", resources / "fonts" / "JetBrainsMono-Regular.ttf", resources / "fonts" / "Lucide.ttf", resources / "images" / "logo.png"]
    required += [resources / "plugins" / plugin / name for plugin in BUNDLED_PLUGINS for name in ("plugin.lua", "translations.lua")]

    for path in required:
        if not path.is_file():
            raise RuntimeError(f"The package carries no \"{path.relative_to(resources)}\"")


def validate_macos_bundle(bundle: Path) -> None:
    run([executable("codesign"), "--verify", "--deep", "--strict", str(bundle)])
    validate_resources(bundle / "Contents" / "Resources")

    if not list((bundle / "Contents" / "Frameworks").glob("libvarn*.dylib")):
        raise RuntimeError("The bundle carries no Lua runtime library in its frameworks")

    # The first line otool prints names the executable itself, so only the libraries after it are read.
    linkage = subprocess.run([executable("otool"), "-L", str(bundle / "Contents" / "MacOS" / "Workpane")], check=True, capture_output=True, text=True).stdout.split("\n", 1)[1]

    if "@rpath/libvarn" not in linkage or str(ROOT) in linkage:
        raise RuntimeError("The executable does not reach its runtime library through the bundle")

    validate_version(bundle / "Contents" / "MacOS" / "Workpane")

    # A page asking for the camera or the microphone ends a bundle that does not say why it uses them, in each language the product speaks.
    information = plistlib.loads((bundle / "Contents" / "Info.plist").read_bytes())
    strings = bundle / "Contents" / "Resources" / "pt.lproj" / "InfoPlist.strings"
    translated = strings.read_text(encoding="utf-8") if strings.is_file() else ""

    for usage in ("NSCameraUsageDescription", "NSMicrophoneUsageDescription"):
        if not information.get(usage) or f'"{usage}"' not in translated:
            raise RuntimeError(f"The bundle does not say in every language why it asks for \"{usage}\"")

    # Notarization refuses a bundle an identity signed without the hardened runtime and a secure timestamp, so only an ad hoc signature goes without them.
    signature = subprocess.run([executable("codesign"), "--display", "--verbose=2", str(bundle)], check=True, capture_output=True, text=True).stderr

    if "Signature=adhoc" in signature:
        return

    if "(runtime)" not in signature or "Timestamp=" not in signature:
        raise RuntimeError("The bundle is signed by an identity without the hardened runtime and the secure timestamp notarization requires")

    # The hardened runtime keeps the camera and the microphone from every page unless the signature grants them.
    granted = plistlib.loads(subprocess.run([executable("codesign"), "--display", "--entitlements", "-", "--xml", str(bundle)], check=True, capture_output=True).stdout)

    for entitlement in ("com.apple.security.device.camera", "com.apple.security.device.audio-input"):
        if granted.get(entitlement) is not True:
            raise RuntimeError(f"The bundle is signed by an identity without the entitlement \"{entitlement}\"")


def validate_installed_tree(root: Path, binary: Path, library: Path) -> None:
    if not binary.is_file():
        raise RuntimeError(f"The package holds no executable at \"{binary.relative_to(root)}\"")

    if not list(library.glob("*varn*")):
        raise RuntimeError(f"The package holds no Lua runtime library in \"{library.relative_to(root)}\"")

    validate_resources(binary.parent.parent / "share" / "workpane")
    validate_version(binary)


# The packaged executable starts from the layout it was installed with and answers the version it was built as.
def validate_version(binary: Path) -> None:
    answer = subprocess.run([str(binary), "--version"], check=True, capture_output=True, text=True).stdout.strip()

    if answer != f"Workpane {current_version()}":
        raise RuntimeError(f"The packaged executable answered \"{answer}\" for its version")


def validate_debian_package(package: Path) -> None:
    with tempfile.TemporaryDirectory() as staging:
        contents = subprocess.run([executable("dpkg-deb"), "--fsys-tarfile", str(package)], check=True, capture_output=True).stdout

        with tarfile.open(fileobj=io.BytesIO(contents)) as archive:
            archive.extractall(staging, filter="tar")

        root = Path(staging)
        validate_installed_tree(root, root / "opt" / "workpane" / "bin" / "Workpane", root / "opt" / "workpane" / "lib")

        if not (root / "usr" / "share" / "applications" / "workpane.desktop").is_file():
            raise RuntimeError("The package installs no desktop entry")

        if not (root / "usr" / "share" / "icons" / "hicolor" / "512x512" / "apps" / "workpane.png").is_file():
            raise RuntimeError("The package installs no application icon")

        command = root / "usr" / "bin" / "workpane"

        if not command.is_symlink() or os.readlink(command) != "/opt/workpane/bin/Workpane":
            raise RuntimeError("The package does not link the application into the command search path")


def task_validate_package(context: Context) -> None:
    build = release_context(context).build_dir
    version = current_version()

    if sys.platform == "darwin":
        validate_macos_bundle(build / "_CPack_Packages" / "Darwin" / "DragNDrop" / f"Workpane-{version}-Darwin" / "Workpane.app")

        if not (build / f"Workpane-{version}-Darwin.dmg").is_file():
            raise RuntimeError(f"No disk image of Workpane {version} was produced")
    elif os.name == "nt":
        if not list(build.glob(f"Workpane-{version}-*.exe")):
            raise RuntimeError(f"No installer of Workpane {version} was produced")

        staged = next((build / "_CPack_Packages").glob(f"*/NSIS/Workpane-{version}-*"), None)

        if staged is None:
            raise RuntimeError("The installer tree was not found")

        validate_installed_tree(staged, staged / "bin" / "Workpane.exe", staged / "bin")

        # The installer carries the runtime of the compiler, which a machine without the redistributable of Visual C++ lacks.
        for runtime in ("msvcp140.dll", "vcruntime140.dll"):
            if not (staged / "bin" / runtime).is_file():
                raise RuntimeError(f"The installer carries no \"{runtime}\"")
    else:
        # A package of an earlier version may still lie in the build folder, so only the one of the current version is read.
        packages = list(build.glob(f"workpane_{version}_*.deb"))

        if len(packages) != 1:
            raise RuntimeError(f"No single Debian package of Workpane {version} was produced")

        validate_debian_package(packages[0])

    print("Package validation succeeded")


def application_data_dir() -> Path:
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Application Support" / "Workpane"

    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA")

        if not base:
            raise RuntimeError("The variable \"LOCALAPPDATA\" is not defined")

        return Path(base) / "Workpane"

    # The product reads the data directory of the desktop specification only when it is absolute, as the specification asks.
    base = os.environ.get("XDG_DATA_HOME", "")
    return Path(base if base.startswith("/") else str(Path.home() / ".local" / "share")) / "workpane"


def task_reset_data(context: Context) -> None:
    directory = application_data_dir()

    if not directory.exists():
        print(f"No application data was found at \"{directory}\"")
        return

    print(f"This permanently removes the Workpane database and every plugin state in \"{directory}\"")

    if context.value != "force" and input("Type the word remove to continue: ").strip() != "remove":
        print("The application data was preserved")
        return

    shutil.rmtree(directory)
    print(f"Removed \"{directory}\"")


def task_clean(context: Context) -> None:
    if (context.build_dir / "CMakeCache.txt").exists():
        cmake_build(context, "clean")


def task_distclean(_: Context) -> None:
    if (ROOT / "build").exists():
        shutil.rmtree(ROOT / "build")


def task_all(context: Context) -> None:
    task_format_check(context)
    task_audit(context)
    task_test(context)


TASKS = {
    "all": (task_all, "Check formatting, run the audits, build and run the tests"),
    "audit": (task_audit, "Run the audits this project declares for itself"),
    "build": (task_build, "Build the application"),
    "clean": (task_clean, "Clean the selected build directory"),
    "configure": (task_configure, "Configure the selected build directory"),
    "coverage": (task_coverage, "Generate the coverage reports"),
    "distclean": (task_distclean, "Remove every build directory"),
    "doctor": (task_doctor, "Check the required and optional development tools"),
    "format": (task_format, "Format every C++ source file"),
    "icons": (task_icons, "Write the table of the glyph names of the bundled Lucide face"),
    "format-check": (task_format_check, "Validate the formatting of every C++ source file"),
    "lint": (task_lint, "Run the audits and then Cppcheck against the product sources"),
    "package": (task_package, "Create the release package of the running platform"),
    "reset-data": (task_reset_data, "Remove the application database and every plugin state"),
    "run": (task_run, "Build and run the application, optionally with a data directory as the value"),
    "sanitize": (task_sanitize, "Build and run the tests with the address and undefined behavior sanitizers"),
    "test": (task_test, "Build and run every test"),
    "validate-package": (task_validate_package, "Validate the assembled release package"),
    "version": (task_version, "Print the version or set a new value in the \"MAJOR.MINOR.PATCH\" format"),
}


def print_tasks() -> None:
    width = max(len(name) for name in TASKS)
    print("Available tasks:\n")

    for name, (_, description) in sorted(TASKS.items()):
        print(f"  {name:<{width}}  {description}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Workpane development tasks")
    parser.add_argument("task", choices=sorted(TASKS), nargs="?")
    parser.add_argument("value", nargs="?")
    parser.add_argument("--configuration", choices=["Debug", "Release", "RelWithDebInfo"], default="Debug")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    parser.add_argument("--verbose", action="store_true")
    arguments = parser.parse_args()

    if arguments.task is None:
        print_tasks()
        return 0

    build_dir = arguments.build_dir or ROOT / "build" / arguments.configuration.lower()
    context = Context(arguments.configuration, build_dir.resolve(), arguments.jobs, arguments.verbose, arguments.value)

    try:
        TASKS[arguments.task][0](context)
    except RuntimeError as error:
        print(f"\nThe task \"{arguments.task}\" failed.\n{error}", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as error:
        print(f"\nThe task \"{arguments.task}\" failed because the command \"{Path(error.cmd[0]).name}\" exited with status {error.returncode}.", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
