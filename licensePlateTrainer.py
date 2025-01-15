from ultralytics import YOLO #type: ignore
import os

# Get absolute path to the data directory
current_dir = os.path.dirname(os.path.abspath(__file__))
data_yaml_path = os.path.join(current_dir, "dataset", "data.yaml")

# Train the YOLO model
# model = YOLO("yolov8n.yaml")  # This will download the base model if not present
model = YOLO('yolov8n.pt')

model.train(
    data=data_yaml_path, 
    epochs=50, 
    imgsz=640,
    batch=16)
