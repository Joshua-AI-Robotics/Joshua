# Data storage and inspection

`//ai/train:data_store` provides the Python `DataStore` library for recording ROS
messages to rosbag2 and exporting Hugging Face datasets, JSONL, CSV, or Parquet.
`//ai/train:data_load` inspects an existing Hugging Face dataset on disk.

## Data store

Construct `DataStore` with an `ai.data_store.SingleDataStore` protobuf. The
`ai.data_stores` config section assigns a `DATA_SUBSCRIBER` node, which the
launcher starts to subscribe and record configured topics. See
`config/config_preset/example/sample_data_store.pbtxt`. Publish Bool messages
on `/recording_control` (true to start, false to stop); shutdown exports the bag.

The library can also be used directly:

```python
from ai.train.data_store import DataStore

store = DataStore(single_data_store_config)
store.start_recording()
store.add_data(message, topic="/joint/position")
store.stop_recording()
store.post_process()
```

Set `store_path`, `data_store_mode`, `data_store_type`, and `node.subscriptions`
(topic and `ros2_data_type`) in the protobuf. The node ID/type are used by the launcher. Direct library callers must call
`add_data` only for configured topics and own their ROS subscriptions and
initialization of the ROS Python environment.

The store records episode indices and exports rows with topic, timestamp,
episode index, and message fields. Images are decoded through cv_bridge;
`ros2/ros2_type_resolver.py` supplies the type names and dataset conversion helpers.
The current `CLOUD_STORAGE` option uses the configured path; cloud synchronization
is not implemented.

## Dataset loader

Run inside the Docker development shell:

```bash
bazel run --config=u22 --config=x86-base \
  --@rules_python//python/config_settings:python_version=3.10 \
  //ai/train:data_load -- --dataset_path=/path/to/dataset --num_samples=5
```

The loader prints schema, metadata, and sample summaries. It does not launch ROS
nodes or access robot hardware. Training is not implemented in this package.
