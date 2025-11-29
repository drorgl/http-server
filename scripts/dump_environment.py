Import("env")
import os
import json


# --- 1. SCons Action to Dump Environment ---
def dump_env_to_json(source, target, env):
    """
    Dumps all construction variables from the environment dictionary
    to a JSON file next to the executable.
    """
    # Get the directory where the executable (target) is built
    build_dir = os.path.dirname(target[0].rstr())
    output_path = os.path.join(build_dir, "env_vars.json")

    variables = env.Dictionary()
    env_data = {}
    for key, value in variables.items():
        if not key.startswith("_"):
            env_data[key] = str(value)

    try:
        with open(output_path, "w") as f:
            json.dump(env_data, f, indent=4)
        print(f"--- ✅ Environment variables dumped to {output_path} ---")
    except Exception as e:
        print(f"--- ❌ Failed to dump environment: {e} ---")


env.AddPostAction("$BUILD_DIR/$PROGNAME$PROGSUFFIX", dump_env_to_json)
