DataStore (ROS 2 → Datasets)  
================================

Overview
--------
`ai/train/data_store.py` records ROS 2 messages to rosbag2 in real-time and post-processes them into ML-friendly datasets (Hugging Face Dataset, JSONL, CSV, Parquet).

Key ideas
---------
- Real-time recording using rosbag2 SequentialWriter
- Post-processing reads the bag and streams one row per message
- Minimal base entry with stable metadata: `{"topic": str, "timestamp": float}`
- Type-specific enrichment delegated to `ros2/ros2_type_resolver.py`:
  - Images (sensor_msgs/Image, CompressedImage) → decoded to numpy and added as `image`
  - Other types → converted via `message_to_ordereddict` and merged into the row

Public surface
--------------
- `DataStore(data_store_config: data_store_pb2.DataStore)`
  - Initializes bag destination based on config (LOCAL_FILE or CLOUD_STORAGE)
  - Registers topics with their ROS 2 type (from enum in proto)
- `add_data(msg: Any, topic: str, timestamp: float | None)`
  - Serializes message and writes to bag with timestamp (ns)
- `post_process(path: str | None)`
  - Converts the bag into a dataset and saves as configured format:
    - JSONL: `data.jsonl`
    - CSV: `data.csv`
    - Parquet: `data.parquet`
    - Hugging Face Dataset: directory with Arrow storage
- `get_total_message_count()`, `get_topic_message_count(topic)`

How post-processing works
-------------------------
1. Read rosbag with `SequentialReader`.
2. For each message:
   - Build `base_entry = {"topic": topic, "timestamp": t_sec}`
   - Call `build_entry_for_message(base_entry, msg_type, msg)` from resolver.
   - Yield a final row (generator style) to `Dataset.from_generator`.
3. Save the dataset in the configured format.

Why it’s fast
-------------
- Image fast-path avoids full dict conversion and raw byte duplication; decodes once to numpy.
- Small base entry reduces allocations; only minimal fields are added.
- Generic types use `message_to_ordereddict` once per message.

Extending for new types
-----------------------
Add or adjust handling in `ros2/ros2_type_resolver.py`:
- Update `ROS2_TYPE_MAPPING` (if needed)
- Extend `add_post_process_feature` for canonical field names
- Extend `build_entry_for_message` for specialized fast paths

Usage
-----
- Configure topics/types in `config/config_preset/*.pbtxt`.
- Run your node that publishes/subscribes and writes via `DataStore.add_data`.
- On shutdown, call `DataStore.post_process(output_dir)`.

Notes and tips
--------------
- Without an explicit `features` schema, HF infers columns. You can later do `dataset.cast_column("image", Image(decode=True))` if needed.
- Keep topics stable by type; rosbag expects one type per topic.
- For resilience, you can wrap deserialization or row building in try/except around your invocation if desired.


