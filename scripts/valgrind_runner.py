import subprocess
import sys
import os
import json


def run_test_with_valgrind():
    """
    Runs the test executable using Valgrind, captures the XML report,
    and fails if Valgrind reports errors.
    """

    if len(sys.argv) < 2:
        print("Error: Missing test executable path.", flush=True)
        sys.exit(1)

    test_executable = sys.argv[1]
    build_dir = os.path.dirname(test_executable)
    env_json_path = os.path.join(build_dir, "env_vars.json")

    # 1. Load Test Name (using the same logic as your gcovr_runner)
    try:
        with open(env_json_path, "r") as f:
            env_data = json.load(f)
        test_name = env_data.get("PIOTEST_RUNNING_NAME")
        if not test_name:
            raise ValueError("'PIOTEST_RUNNING_NAME' not found in environment data.")
    except (FileNotFoundError, ValueError) as e:
        print(f"Error loading environment data: {e}", flush=True)
        sys.exit(1)

    print(f"--- 🛡️ Running Valgrind Check for '{test_name}' ---", flush=True)

    # 2. Define Output Paths
    reports_dir = os.path.join(".pio", "tests")
    os.makedirs(reports_dir, exist_ok=True)

    # Valgrind XML report path
    xml_report_path = os.path.join(reports_dir, f"valgrind_xml_{test_name}.xml")

    # Simple Text Summary/Log (useful for quick checking)
    text_log_path = os.path.join(reports_dir, f"memory_{test_name}.txt")

    # 3. Construct the Valgrind Command
    valgrind_command = [
        "valgrind",
        "--tool=memcheck",
        "--leak-check=full",
        "--show-reachable=yes",
        "--track-origins=yes",
        # Set exit code to 1 if any errors are found (ensures test failure)
        "--error-exitcode=1",
        # Generate XML for later automated parsing (e.g., in CI)
        # "--xml=yes",
        # f"--xml-file={xml_report_path}",
        # Target executable and arguments (if any)
        test_executable,
    ]

    # 4. Run Valgrind
    try:
        # We redirect stderr to the text log, as Valgrind prints its summary/errors there.
        with open(text_log_path, "w") as log_file:
            result = subprocess.run(
                valgrind_command,
                check=False,  # Don't raise exception on non-zero exit code yet
                stderr=log_file,
                capture_output=False,
            )

        # 5. Check Valgrind's Exit Code
        if result.returncode == 0:
            print(f"--- ✅ Valgrind Check for '{test_name}' Passed ---", flush=True)
            sys.exit(0)
        else:
            print(f"--- ❌ Valgrind Check for '{test_name}' Failed! ---", flush=True)
            print(f"Memory errors detected. Check log: {text_log_path}", flush=True)
            # Exit with the error code to fail the PlatformIO test
            sys.exit(1)

    except FileNotFoundError:
        print("Error: 'valgrind' command not found. Is Valgrind installed and in your PATH?", flush=True)
        sys.exit(1)
    except Exception as e:
        print(f"An unexpected error occurred during Valgrind run: {e}", flush=True)
        sys.exit(1)


if __name__ == "__main__":
    run_test_with_valgrind()
