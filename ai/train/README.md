AI Training Pipeline
====================

This directory contains the tools and infrastructure for collecting data, managing datasets, and training AI models for the Joshua platform.

DataStore (Data Collection)
---------------------------

### Overview
`ai/train/data_store.py` is the core data logging engine. It subscribes to ROS 2 topics in real-time, records them efficiently to `rosbag2` (SQLite3), and post-processes them into machine-learning-ready formats (Hugging Face Datasets, JSONL, CSV, Parquet).

### Key Features
-   **Robust Schema Handling**: Automatically discovers all possible fields across heterogeneous topics (e.g., Images + Encoder values) to ensure a consistent, crash-free dataset schema.
-   **Episode Indexing**: Maintains a persistent, auto-incrementing global episode counter across runs to prevent data overwrites and simplify merging.
-   **Real-time Control**: Supports dynamic Start/Stop recording via ROS topics or internal logic.
-   **Optimized Post-Processing**:
    -   Images are decoded to Numpy once and stored efficiently.
    -   Generic messages are converted to dictionaries.
    -   Sparse data handling: Missing fields are automatically filled with `None` to satisfy strict dataset schemas.

### Architecture
1.  **Recording (Online)**:
    -   Uses `rosbag2_py.SequentialWriter` for high-throughput, low-latency logging.
    -   Writes interleaved message streams (preserving exact timing).
2.  **Post-Processing (Offline/Shutdown)**:
    -   **Pass 1 (Discovery)**: Scans the bag to identify all unique topics and their field structures.
    -   **Pass 2 (Conversion)**: Streams the bag, enforces the unified schema, injects `episode_index`, and writes to the target format using `datasets.Dataset.from_generator`.

### Usage

**1. Configuration**
Define your data sources in a `.pbtxt` config file (e.g., `config/config_preset/sample_data_store.pbtxt`).

**2. Running the Data Subscriber**
The `ros2/data_subscriber.py` node wraps the DataStore.
```bash
# Launch the subscriber
bazel run launcher:joshua_main -- --config=config/config_preset/sample_data_store.pbtxt
```

**3. Controlling Recording**
Control the recording state via the `/recording_control` topic:
```bash
# Start Recording (Episode N)
ros2 topic pub --once /recording_control std_msgs/msg/Bool "{data: true}"

# Stop Recording
ros2 topic pub --once /recording_control std_msgs/msg/Bool "{data: false}"
```

**4. Data Inspection**
Use the provided utility to inspect generated datasets:
```bash
# View schema, metadata, and samples
bazel run ai/train:data_load -- --dataset_path=/tmp/Joshua/data/..._processed --num_samples=5
```

### Dataset Format
The output is an **Interleaved Message Stream**. Each row corresponds to a single ROS message event.
-   `topic`: The source topic name (e.g., `camera_1`, `encoder_joint_1`).
-   `timestamp`: Float (seconds).
-   `episode_index`: Integer ID for the recording session.
-   `image`: Numpy array (for camera topics, else None).
-   `data`: Scalar/Value (for standard messages, else None).
-   *(Other fields dynamically discovered from message types)*

*Note: For training (e.g., LeRobot, Octo), this interleaved data typically needs to be synchronized/resampled into state-action pairs.*

Extending Types (`ros2/ros2_type_resolver.py`)
------------------------------------------------
-   **New Message Types**: Add them to `ROS2_TYPE_MAPPING`.
-   **Special Handling**: Extend `build_entry_for_message` if you need custom decoding (like we do for Images) instead of generic dictionary conversion.

Training & Fine-Tuning
----------------------
*(Section to be expanded)*
-   **LeRobot Integration**: The dataset format is compatible with Hugging Face Datasets, making it a natural fit for the [LeRobot](https://github.com/huggingface/lerobot) framework.
-   **Preprocessing**: Use `LeRobotDataset` to synchronize the interleaved `DataStore` output into `(observation, action)` batches for training.


