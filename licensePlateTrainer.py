# from ultralytics import YOLO

# # Train the YOLO model
# model = YOLO("yolov8n.yaml")  # Use YOLOv8 configuration
# model.train(data="License Plate Recognition/data.yaml", epochs=50, imgsz=640)
from ultralytics import YOLO
import os

# Get absolute path to the data directory
current_dir = os.path.dirname(os.path.abspath(__file__))
data_yaml_path = os.path.join(current_dir, "License Plate Recognition", "data.yaml")

# # Train the YOLO model
model = YOLO("yolov8n.yaml")  # This will download the base model if not present
model.train(data=data_yaml_path, epochs=50, imgsz=640)

# # Save the trained model
model.save('license_plate_detector.pt')