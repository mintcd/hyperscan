import json
import csv
import subprocess
from pathlib import Path

BASE_DIR = Path(__file__).parent.parent.resolve()
REGEX_FILE = BASE_DIR / "dataset" / "regexes.txt"
EXE_FILE = BASE_DIR / "build_msvc" / "bin" / "dump_rose.exe"
OUT_DIR = BASE_DIR / "output" / "info"
STATISTICS_FILE = OUT_DIR / "statistics.csv"


def main():    
    with open(REGEX_FILE, 'r', encoding='utf-8') as f:
        regexes = [line.strip() for line in f if line.strip()]

    stats = []

    print(f"Starting processing {len(regexes)} regexes...")

    for i, regex in enumerate(regexes):
        json_path = OUT_DIR / f"{i}.json"
        
        # Call dump_rose
        try:
            subprocess.run(
                [str(EXE_FILE), regex, str(json_path)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=5  # Prevent hanging on pathological regexes
            )
        except subprocess.TimeoutExpired:
            print(f"[!] Timeout expired for regex {i}: {regex}")
            continue
        except Exception as e:
            print(f"[!] Error running dump_rose for regex {i}: {e}")
            continue

        # 2. Parse JSON
        if not json_path.exists():
            print(f"[-] No output for regex {i}: {regex}")
            continue

        try:
            with open(json_path, 'r', encoding='utf-8') as jf:
                data = json.load(jf)
            
            num_fas = len(data.get("FAs", []))
            # Count roles that are actual literals (exclude root/anchored_root)
            num_literals = len([r for r in data.get("roles", []) if r.get("id") not in ("root", "anchored_root") and r.get("literal") != ""])
            
            stats.append({"index": i, "regex": regex, "num_literals": num_literals, "num_FAs": num_fas})
        except Exception as e:
            print(f"[!] Error parsing JSON for regex {i}: {e}")

    # 3. Output to CSV
    with open(STATISTICS_FILE, 'w', newline='', encoding='utf-8') as cf:
        writer = csv.DictWriter(cf, fieldnames=["index", "regex", "num_literals", "num_FAs"])
        writer.writeheader()
        writer.writerows(stats)
    
    print(f"\nDone! Successfully processed {len(stats)} regexes and saved to {STATISTICS_FILE}")

if __name__ == "__main__":
    main()