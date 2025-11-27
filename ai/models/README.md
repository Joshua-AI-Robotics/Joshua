# Model Registry

The Model Registry (`model_registry.py`) is the central place where AI model implementations are mapped to their configuration enums. This allows the inference system to dynamically instantiate the correct model class based on the configuration provided.

## How it Works

The `MODEL_REGISTRY` dictionary maps a `ModelType` enum value (defined in `ai/proto/ai_model.proto`) to a Python class that inherits from `ModelBase`.

When the inference node starts, it looks up the `model_type` from the configuration in this registry to find the corresponding class to instantiate.

## How to Add a New Model

To add a new AI model to the system, follow these steps:

### 1. Create the Model Class

Create a new directory for your model (e.g., `ai/models/my_new_model/`) and implement your model class inheriting from `ModelBase`.

```python
from ai.models.model_base import ModelBase

class MyNewModel(ModelBase):
    def __init__(self, config):
        super().__init__(config)
        # Your initialization here

    def handle_input(self, subscription_index, data, publish_callback):
        # Your logic here
        pass
    
    # Implement other abstract methods...
```

### 2. Update Protocol Buffers

Modify `ai/proto/ai_model.proto` to register your new model type.

1.  Add a new enum value to `ModelType`:
    ```protobuf
    enum ModelType {
      // ...
      MY_NEW_MODEL = 2;
    }
    ```

2.  Add your model's specific configuration message (if any) to the `oneof model_config` in `SingleModel`:
    ```protobuf
    message SingleModel {
      // ...
      oneof model_config {
        RandomNoiseConfig random_noise_config = 6;
        MyNewModelConfig my_new_model_config = 7;
      }
    }
    ```

### 3. Register the Model

Update `ai/models/model_registry.py` to include your new model.

1.  Import your model class:
    ```python
    from ai.models.my_new_model.my_new_model import MyNewModel
    ```

2.  Add it to the `MODEL_REGISTRY`:
    ```python
    MODEL_REGISTRY: Dict[int, Type[ModelBase]] = {
        ai_model_pb2.ModelType.RANDOM_NOISE: RandomNoise,
        ai_model_pb2.ModelType.MY_NEW_MODEL: MyNewModel,
    }
    ```

### 4. Update Build Dependencies

Finally, ensure the build system knows about the dependency. Update `ai/models/BUILD`:

```python
py_library(
    name = "model_registry",
    srcs = ["model_registry.py"],
    deps = [
        # ... other deps ...
        "//ai/models/my_new_model:my_new_model",  # Add your model target here
    ],
)
```

