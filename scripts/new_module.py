"""Create a module skeleton without changing the application's wiring."""

import argparse
import re
from pathlib import Path
from string import Template


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name", help="lowercase snake_case, for example temperature_sensor")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-z][a-z0-9]*(?:_[a-z0-9]+)*", args.name) or len(args.name) > 40:
        parser.error("Use a name of up to 40 lowercase letters/digits with single underscores.")
    reserved = {"con", "prn", "aux", "nul"} | {
        f"{prefix}{number}" for prefix in ("com", "lpt") for number in range(1, 10)
    }
    if args.name in reserved:
        parser.error("This name is reserved by the Windows filesystem.")

    root = Path(__file__).resolve().parent.parent
    destination = root / "components" / f"bth_{args.name}"
    if destination.exists():
        parser.error(f"Already exists: {destination}")

    values = {
        "name": args.name,
        "class_name": "".join(p.title() for p in args.name.split("_")) + "Module",
    }
    templates = root / "templates" / "module"
    files = {
        "CMakeLists.txt.in": destination / "CMakeLists.txt",
        "module.hpp.in": destination / "include" / "bth" / args.name / f"{args.name}.hpp",
        "module.cpp.in": destination / "src" / f"{args.name}.cpp",
    }
    # Validate and render every template before creating the destination.
    rendered = {
        target: Template((templates / template).read_text(encoding="utf-8")).substitute(values)
        for template, target in files.items()
    }
    destination.mkdir()  # Fails instead of overwriting if another process created it.
    for target, content in rendered.items():
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open("x", encoding="utf-8", newline="\n") as output:
            output.write(content)
    print(f"Created components/bth_{args.name}")
    print("Next: implement the module, add host tests, wire it in main/application.cpp")
    print(f"and add bth_{args.name} to main/CMakeLists.txt. See docs/ADDING_MODULE.md.")


if __name__ == "__main__":
    main()
