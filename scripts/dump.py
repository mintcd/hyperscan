import subprocess
import os
from pathlib import Path

BASE_DIR = Path(__file__).parent.parent.resolve()
OUT_DIR = BASE_DIR / 'output' / 'dump'
ROSE_DUMP_EXE = BASE_DIR / 'build_msvc' / 'bin' / 'dump_rose.exe'
IN_PATH = BASE_DIR / 'dataset' / 'regexes.txt'


def dumpRegex(rose_dump_exe, regex, out_path):
    subprocess.run([str(rose_dump_exe), regex, str(out_path)],
                   stdout=subprocess.PIPE, text=True, check=True)
    
def dumpAll(rose_dump_exe, in_path, out_dir):
    """Read a text file with one regex per line and dump JSONs.

    Lines that are empty or start with '#' are ignored. Output files are
    written as numeric JSON files: 0.json, 1.json, ... inside `out_dir`.
    """
    in_path = Path(in_path)
    out_dir = Path(out_dir)
    os.makedirs(out_dir, exist_ok=True)

    with open(in_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    idx = 0
    for line in lines:
        regex = line.strip()
        if not regex or regex.startswith('#'):
            continue

        out_path = out_dir / f"{idx}.json"
        try:
            dumpRegex(rose_dump_exe, regex, out_path)
        except subprocess.CalledProcessError as e:
            print(f"dump_rose failed for index {idx}, regex={regex!r}: {e}")
        idx += 1

if __name__ == "__main__":
    dumpAll(ROSE_DUMP_EXE, IN_PATH, OUT_DIR)