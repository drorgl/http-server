import subprocess
import sys
import os
import json

def run_test_with_sanitizers():
    """
    Runs the test executable with ASan/UBSan environment variables set.
    Captures stderr (where sanitizer reports are printed) to a log file.
    """

    if len(sys.argv) < 2:
        print("Error: Missing test executable path.", flush=True)
        sys.exit(1)

    test_executable = sys.argv[1]
    build_dir = os.path.dirname(test_executable)
    env_json_path = os.path.join(build_dir, "env_vars.json")

    # 1. Load Test Name (using the same logic as your previous runners)
    try:
        with open(env_json_path, "r") as f:
            env_data = json.load(f)
        test_name = env_data.get("PIOTEST_RUNNING_NAME")
        if not test_name:
            raise ValueError("'PIOTEST_RUNNING_NAME' not found in environment data.")
    except (FileNotFoundError, ValueError) as e:
        print(f"Error loading environment data: {e}", flush=True)
        sys.exit(1)

    print(f"--- 🛡️ Running ASan/UBSan Check for '{test_name}' ---", flush=True)

    # 2. Define Output Paths and Environment Variables
    reports_dir = os.path.join(".pio", "tests")
    os.makedirs(reports_dir, exist_ok=True)
    
    # Sanitizer report log path (ASan/UBSan print to stderr by default)
    log_path = os.path.join(reports_dir, f"asan_ubsan_{test_name}.log")

    # IMPORTANT: Configure ASan/LSan via environment variable
    # - detect_leaks=1: Enables LeakSanitizer (LSan)
    # - exitcode=1: Ensures the program exits with 1 if any error is found (critical for CI)
    asan_env_vars = os.environ.copy()
    asan_env_vars["ASAN_OPTIONS"] = "detect_leaks=1:exitcode=1"
    
    # If using MSan, you would add MSAN_OPTIONS here too.
    # We don't explicitly need UBSAN_OPTIONS for this level of detail.

    # 3. Run the test executable
    try:
        # ASan/UBSan reports are written to stderr. We redirect stderr to our log file.
        with open(log_path, "w") as log_file:
            result = subprocess.run(
                [test_executable],
                check=False,
                env=asan_env_vars, # Pass the environment variables
                stderr=log_file,   # Capture sanitizer reports
                capture_output=False,
            )

        # 4. Check the Test's Exit Code
        # The ASAN_OPTIONS=exitcode=1 setting ensures the executable returns 1 if errors were found.
        if result.returncode == 0:
            print(f"--- ✅ ASan/UBSan Check for '{test_name}' Passed ---", flush=True)
            sys.exit(0)
        else:
            print(f"--- ❌ ASan/UBSan Check for '{test_name}' Failed! ---", flush=True)
            print(f"Memory errors detected. Check log: {log_path}", flush=True)
            # Exit with the error code to fail the PlatformIO test
            sys.exit(1)

    except Exception as e:
        print(f"An unexpected error occurred during sanitizer run: {e}", flush=True)
        sys.exit(1)

if __name__ == "__main__":
    run_test_with_sanitizers()