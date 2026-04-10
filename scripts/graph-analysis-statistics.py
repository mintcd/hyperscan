import os
import sys
import json
import csv
import subprocess
from pathlib import Path

def main():
    base_dir = Path(__file__).parent.resolve()
    regex_file = Path(r"D:\Projects\hyperscan\dataset\regexes.txt")
    out_dir = Path(r"D:\Projects\hyperscan\output\graphs")
    
    # Create output directory if it doesn't exist
    out_dir.mkdir(parents=True, exist_ok=True)
    csv_file = out_dir / "statistics.csv"

    # Determine executable extension and path
    exe_suffix = ".exe" if os.name == "nt" else ""
    dump_rose_exe = Path(r"D:\Projects\hyperscan\build_msvc\bin\dump_rose.exe")
    
    if not dump_rose_exe.exists():
        print(f"ERROR: Cannot find dump_rose executable at {dump_rose_exe}")
        print("Please build the examples first.")
        sys.exit(1)

    if not regex_file.exists():
        print(f"ERROR: Cannot find regex file at {regex_file}")
        sys.exit(1)

    with open(regex_file, 'r', encoding='utf-8') as f:
        regexes = [line.strip() for line in f if line.strip()]

    stats = []

    print(f"Starting processing of {len(regexes)} regexes...")

    for i, regex in enumerate(regexes):
        json_path = out_dir / f"{i}.json"
        
        # 1. Call dump_rose
        try:
            subprocess.run(
                [str(dump_rose_exe), regex, str(json_path)],
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
            print(f"[-] No JSON output for regex {i} (compilation might have failed)")
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
    with open(csv_file, 'w', newline='', encoding='utf-8') as cf:
        writer = csv.DictWriter(cf, fieldnames=["index", "regex", "num_literals", "num_FAs"])
        writer.writeheader()
        writer.writerows(stats)
    
    print(f"\nDone! Successfully processed {len(stats)} regexes and saved to {csv_file}")

if __name__ == "__main__":
    main()