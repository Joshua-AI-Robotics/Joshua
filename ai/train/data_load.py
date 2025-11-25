# This is util script to inspect the dataset after it has been saved.
from datasets import load_from_disk

dataset = load_from_disk('/tmp/Joshua/data/sample_recordings/dataset_20251121_182433_processed')

print("Dataset loaded successfully!")
print(dataset)

print("--- 1. OVERALL DATASET OBJECT ---")
print(dataset)

print("\n--- 2. DETAILED FEATURES (SCHEMA) ---")
# If you have splits (like 'train'), access one to see the schema
if hasattr(dataset, 'keys'):
    first_split = list(dataset.keys())[0]
    print(f"Features for split '{first_split}':")
    print(dataset[first_split].features)
else:
    # If it's just a single dataset without splits
    print(dataset.features)

print("\n--- 3. DATASET METADATA ---")
# This often contains the description, citation, and homepage
# (Note: might be empty if the dataset was saved locally without this info)
if hasattr(dataset, 'keys'):
    print(dataset[list(dataset.keys())[0]].info.description)
else:
    print(dataset.info.description)

print("\n--- 4. SAMPLE DATA (first 3 examples) ---")
# Determine which dataset to sample from (split vs single dataset)
if hasattr(dataset, 'keys'):
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