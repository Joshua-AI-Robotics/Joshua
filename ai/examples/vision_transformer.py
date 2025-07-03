import cv2
from PIL import Image
import torch
from transformers import ViTForImageClassification, ViTImageProcessor

# 1. Load a pre-trained Vision Transformer model and its processor
# You can choose different ViT models, e.g., 'google/vit-base-patch16-224'
model_name = 'google/vit-base-patch16-224'
processor = ViTImageProcessor.from_pretrained(model_name)
model = ViTForImageClassification.from_pretrained(model_name)

# Set the model to evaluation mode
model.eval()

# Move model to GPU if available
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
model.to(device)

print(f"Using device: {device}")
print(f"Loaded ViT model: {model_name}")

# 2. Initialize webcam
cap = cv2.VideoCapture(0) # 0 for default webcam, change if you have multiple

if not cap.isOpened():
    print("Error: Could not open webcam.")
    exit()

print("Press 'q' to quit.")

while True:
    ret, frame = cap.read() # Read a frame from the webcam

    if not ret:
        print("Failed to grab frame.")
        break

    # Convert OpenCV BGR image to PIL RGB image (required by ViTImageProcessor)
    # OpenCV reads images as BGR, PIL/Transformers expect RGB
    cv2_rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    pil_image = Image.fromarray(cv2_rgb_frame)

    # 3. Preprocess the image for the ViT model
    inputs = processor(images=pil_image, return_tensors="pt").to(device)

    # 4. Perform inference
    with torch.no_grad():
        outputs = model(**inputs)
        logits = outputs.logits

    # Get the predicted class ID
    predicted_class_idx = logits.argmax(-1).item()

    # Get the human-readable label
    predicted_label = model.config.id2label[predicted_class_idx]

    # 5. Display the prediction on the frame
    font = cv2.FONT_HERSHEY_SIMPLEX
    cv2.putText(frame, f"Prediction: {predicted_label}", (10, 30), font, 1, (0, 255, 0), 2, cv2.LINE_AA)

    # Display the frame
    cv2.imshow('ViT Webcam Demo', frame)

    # Exit if 'q' is pressed
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

# Release the webcam and destroy all OpenCV windows
cap.release()
cv2.destroyAllWindows()