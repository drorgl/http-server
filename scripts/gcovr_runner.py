import subprocess
import sys
import os
import json


def run_test_and_collect_coverage():
    """
    Reads env vars from JSON, runs the test executable,
    and collects gcovr data for the current test.
    """

    if len(sys.argv) < 2:
        print("Error: Missing test executable path.", flush=True)
        sys.exit(1)

    test_executable = sys.argv[1]
    build_dir = os.path.dirname(test_executable)
    env_json_path = os.path.join(build_dir, "env_vars.json")

    # 1. Load Environment Data from JSON
    try:
        with open(env_json_path, "r") as f:
            env_data = json.load(f)
    except FileNotFoundError:
        print(f"Error: Environment file not found at {env_json_path}, make sure dump_environment.py is in extra_scripts", flush=True)
        sys.exit(1)
    except Exception as e:
        print(f"Error reading JSON: {e}", flush=True)
        sys.exit(1)

    # 2. Extract Test Name and Build Dir
    # We rely on the SCons script having successfully dumped PIOTEST_RUNNING_NAME
    # Note: PIOTEST_RUNNING_NAME may not exist if PIO changes its internal structure.
    # Replace 'PIOTEST_RUNNING_NAME' if you find a different variable in your JSON dump.
    test_name = env_data.get("PIOTEST_RUNNING_NAME")

    if not test_name:
        print("Error: 'PIOTEST_RUNNING_NAME' not found in environment data.", flush=True)
        # Fallback needed if the variable name is wrong, but we'll halt for debugging
        sys.exit(1)

    print(f"--- 🧪 Running Test '{test_name}' ---", flush=True)

    # 3. Run the test executable (generates .gcda files)
    try:
        subprocess.run([test_executable], check=True, capture_output=False)
        print(f"--- ✅ Test '{test_name}' Finished Successfully ---", flush=True)

    except subprocess.CalledProcessError as e:
        print(f"--- ❌ Test '{test_name}' Failed with code {e.returncode} ---", flush=True)
        sys.exit(e.returncode)

    except Exception as e:
        print(f"--- ❌ Test '{test_name}' Failed with code {e.returncode} ---", flush=True)
        exit(1)

    # 4. Run gcovr for Per-Test Collection
    print(f"--- 📊 Running gcovr Collection for '{test_name}' ---", flush=True)

    os.makedirs(".pio/tests/", exist_ok=True)

    gcovr_command = [
        "gcovr",
        build_dir,
        "--json",
        os.path.join(".pio/tests/", test_name + ".json"),
        "--root",
        ".",
    ]

    try:
        subprocess.run(gcovr_command, check=True, capture_output=False)
        print(f"--- 📝 JSON Trace File created for '{test_name}' ---", flush=True)

    except subprocess.CalledProcessError as e:
        print(f"--- ⚠️ gcovr Collection Failed for '{test_name}' ---", flush=True)


if __name__ == "__main__":
    run_test_and_collect_coverage()
