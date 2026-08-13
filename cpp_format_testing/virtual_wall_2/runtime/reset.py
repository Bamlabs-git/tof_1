#!/usr/bin/env python3
"""Reset runtime recordings and logs for the virtual wall monitor."""

from pathlib import Path
import shutil


RESET_DIRS = ["recorded_actions", "logs"]


def main() -> None:
    runtime_dir = Path(__file__).resolve().parent

    for dirname in RESET_DIRS:
        target = runtime_dir / dirname
        if target.exists():
            shutil.rmtree(target)
            print(f"Deleted {target}")
        target.mkdir(parents=True, exist_ok=True)
        print(f"Created {target}")

    print("Runtime recordings and logs reset.")


if __name__ == "__main__":
    main()
