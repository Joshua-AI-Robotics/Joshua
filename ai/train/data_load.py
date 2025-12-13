import argparse
import sys
from datasets import load_from_disk


def main():
    """
    Utility script to inspect a saved HuggingFace dataset.

    Usage:
        bazel run ai/train:data_load -- /path/to/dataset_processed
    """
    parser = argparse.ArgumentParser(description="Inspect a saved HuggingFace dataset.")
    parser.add_argument(
        "dataset_path",
        type=str,
        help="Path to the dataset directory (e.g. /tmp/Joshua/data/..._processed)",
    )
    args = parser.parse_args()

    try:
        dataset = load_from_disk(args.dataset_path)
    except Exception as e:
        print(f"Error loading dataset from {args.dataset_path}: {e}")
        sys.exit(1)

    print("Dataset loaded successfully!")
    print(dataset)

    print("--- 1. OVERALL DATASET OBJECT ---")
    print(dataset)

    print("\n--- 2. DETAILED FEATURES (SCHEMA) ---")
    # If you have splits (like 'train'), access one to see the schema
    if hasattr(dataset, "keys"):
        first_split = list(dataset.keys())[0]
        print(f"Features for split '{first_split}':")
        print(dataset[first_split].features)
    else:
        # If it's just a single dataset without splits
        print(dataset.features)

    print("\n--- 3. DATASET METADATA ---")
    # This often contains the description, citation, and homepage
    # (Note: might be empty if the dataset was saved locally without this info)
    if hasattr(dataset, "keys"):
        print(dataset[list(dataset.keys())[0]].info.description)
    else:
        print(dataset.info.description)

    print("\n--- 4. SAMPLE DATA (first 3 examples) ---")
    # Determine which dataset to sample from (split vs single dataset)
    if hasattr(dataset, "keys"):
        ds_to_sample = dataset[first_split]
    else:
        ds_to_sample = dataset

    # Decide how many samples to show
    try:
        num_samples = min(3, len(ds_to_sample))
    except Exception:
        num_samples = 3

    print(f"Showing {num_samples} sample example(s):")
    for i in range(num_samples):
        print(f"\nSample {i}:")
        example = ds_to_sample[i]
        for key, value in example.items():
            display_value = value
            if isinstance(value, str) and len(value) > 200:
                display_value = value[:200] + "..."
            elif isinstance(value, bytes):
                display_value = f"<bytes len={len(value)}>"
            elif isinstance(value, list):
                display_value = f"<list len={len(value)}>"
            elif isinstance(value, dict):
                # Show up to 5 keys to avoid verbose output
                display_value = f"<dict keys={list(value.keys())[:5]}>"
            print(f"  {key}: {display_value}")


if __name__ == "__main__":
    main()
