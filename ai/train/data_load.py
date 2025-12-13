import sys
import gflags
import glog
from datasets import load_from_disk
import numpy as np

FLAGS = gflags.FLAGS

gflags.DEFINE_string(
    "dataset_path",
    None,
    "Path to the dataset directory (e.g. /tmp/Joshua/data/..._processed)",
)
gflags.DEFINE_integer(
    "num_samples", 3, "Number of samples to inspect per split."
)
gflags.DEFINE_bool(
    "show_schema", True, "Whether to print the detailed dataset schema."
)
gflags.DEFINE_bool(
    "show_metadata", True, "Whether to print dataset metadata."
)


def inspect_value(value, indent="  "):
    """Recursively inspect structure of values without printing massive arrays."""
    if value is None:
        return "None"
    
    if isinstance(value, (str, int, float, bool)):
        s = str(value)
        return s[:200] + "..." if len(s) > 200 else s
        
    if isinstance(value, bytes):
        return f"<bytes len={len(value)}>"
        
    if isinstance(value, np.ndarray):
        return f"<np.ndarray shape={value.shape} dtype={value.dtype}>"

    # Handle HuggingFace dataset specific types or lists
    if isinstance(value, list):
        if not value:
            return "[]"
        # Peek at first element type
        first_elem = inspect_value(value[0], indent + "  ")
        return f"<list len={len(value)}, type=[{first_elem}, ...]>"

    if isinstance(value, dict):
        if not value:
            return "{}"
        keys_preview = ", ".join(list(value.keys())[:5])
        if len(value) > 5:
            keys_preview += "..."
        return f"<dict keys={{{keys_preview}}}>"

    return str(type(value))


def main(argv):
    try:
        argv = FLAGS(argv)
    except gflags.FlagsError as e:
        print(f"{e}\nUsage: {sys.argv[0]} ARGS\\n{FLAGS}")
        sys.exit(1)

    if not FLAGS.dataset_path:
        print("Error: --dataset_path is required.")
        print(f"Usage: {sys.argv[0]} --dataset_path <path>")
        sys.exit(1)

    glog.info(f"Loading dataset from: {FLAGS.dataset_path}")

    try:
        dataset = load_from_disk(FLAGS.dataset_path)
    except Exception as e:
        glog.error(f"Error loading dataset: {e}")
        sys.exit(1)

    glog.info("Dataset loaded successfully!")

    print("\n" + "=" * 50)
    print(" 1. OVERALL DATASET STRUCTURE ")
    print("=" * 50)
    print(dataset)

    if FLAGS.show_schema:
        print("\n" + "=" * 50)
        print(" 2. DETAILED FEATURES (SCHEMA) ")
        print("=" * 50)
        if hasattr(dataset, "keys"):
            for split in dataset.keys():
                print(f"\n[Split: {split}]")
                print(dataset[split].features)
        else:
            print(dataset.features)

    if FLAGS.show_metadata:
        print("\n" + "=" * 50)
        print(" 3. DATASET METADATA ")
        print("=" * 50)
        info = dataset[list(dataset.keys())[0]].info if hasattr(dataset, "keys") else dataset.info
        print(f"Description: {info.description}")
        print(f"Version:     {info.version}")
        print(f"Homepage:    {info.homepage}")
        print(f"License:     {info.license}")

    print("\n" + "=" * 50)
    print(f" 4. SAMPLE DATA ({FLAGS.num_samples} examples)")
    print("=" * 50)

    # Determine which dataset to sample from
    if hasattr(dataset, "keys"):
        first_split = list(dataset.keys())[0]
        ds_to_sample = dataset[first_split]
    else:
        ds_to_sample = dataset

    num_samples = min(FLAGS.num_samples, len(ds_to_sample))

    for i in range(num_samples):
        print(f"\nSample {i}:")
        print("-" * 20)
        example = ds_to_sample[i]
        
        # Sort keys for consistent display
        for key in sorted(example.keys()):
            val_str = inspect_value(example[key])
            print(f"{key:<20}: {val_str}")


if __name__ == "__main__":
    main(sys.argv)
