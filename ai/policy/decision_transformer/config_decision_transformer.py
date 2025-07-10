from transformers.models.decision_transformer.configuration_decision_transformer import (
    DecisionTransformerConfig as HFDecisionTransformerConfig,
)


class DecisionTransformerConfig(HFDecisionTransformerConfig):
    """
    This is the configuration class for a [`DecisionTransformer`].
    It extends the standard DecisionTransformerConfig with parameters for our
    custom vision and motor encoders.
    """

    model_type = "decision_transformer"

    def __init__(
        self,
        # Custom vision/motor parameters
        image_size_h=128,
        image_size_w=128,
        embedding_dim=128,
        non_visual_state_dim=6,
        # Other standard DT parameters can be passed in kwargs
        **kwargs,
    ):
        # Assign our custom parameters to the instance.
        self.image_size_h = image_size_h
        self.image_size_w = image_size_w
        self.embedding_dim = embedding_dim
        self.non_visual_state_dim = non_visual_state_dim

        # Calculate the full state dimension for the parent class.
        state_dim = embedding_dim + non_visual_state_dim
        kwargs["state_dim"] = state_dim

        # Call the parent constructor with all arguments.
        super().__init__(**kwargs)

    @classmethod
    def from_proto(cls, proto_message):
        """
        Creates a DecisionTransformerConfig from the main Config protobuf message.
        This method parses the protobuf message to extract all necessary
        hyperparameters for the model, including custom parameters and standard
        Hugging Face transformer parameters.
        Args:
            proto_message: The loaded top-level Config protobuf object.
        Returns:
            An instance of DecisionTransformerConfig.
        """
        if proto_message is None:
            return cls()

        ai_config = proto_message.ai
        robot_config = proto_message.robot
        dt_config = ai_config.decision_transformer_config

        # --- Dynamically determine dimensions from robot config ---
        
        # Action dimension from number of actuators
        act_dim = len(robot_config.actuations.single_actuation)

        # State dimensions from perception sensors
        image_size_h = 128  # Default
        image_size_w = 128  # Default
        non_visual_state_size = 0
        
        for perception in robot_config.perceptions.single_perception:
            sensor = perception.sensor
            # The sensor_type enum for CAMERA is 1.
            if sensor.sensor_type == 1: # CAMERA
                # For a 'oneof', we must check which field is active.
                if sensor.WhichOneof('sensor_config') == 'camera_config':
                    cam_config = sensor.camera_config
                    # Use configured height/width, but provide a sensible default if not set.
                    image_size_h = cam_config.height or 128
                    image_size_w = cam_config.width or 128
            else:
                non_visual_state_size += 1
        
        # Create a dictionary of all parameters for the constructor
        config_dict = {
            "image_size_h": image_size_h,
            "image_size_w": image_size_w,
            "non_visual_state_dim": non_visual_state_size,
            "embedding_dim": dt_config.vision_embedding_dim,
            "hidden_size": dt_config.hidden_size,
            "n_layer": dt_config.n_layer,
            "n_head": dt_config.n_head,
            "activation_function": dt_config.activation_function,
            "dropout": dt_config.dropout,
            "n_inner": dt_config.n_inner,
            "max_length": dt_config.context_length,
            "act_dim": act_dim,
        }

        # Instantiate the class with the parsed parameters
        return cls(**config_dict) 