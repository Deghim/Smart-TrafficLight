from ultralytics import YOLO
import cv2 # libreria utilizada para uso de imagenes

# Load Models
coco_model = YOLO('yolov8n.pt') # Modelo de YOLO ya entrenado usado para detectar carros
# license_plate_detector  = YOLO('path al modelo entreado para placas' ) # Se crea un modelo que detecta placas

# Load Videos
cap = cv2.VideoCapture('testvideos/27260-362770008_tiny.mp4') # Se utiliza un video para testear el modelo

# Read Frames
ret = True # Detecta que se hayan leido los frames
frame_number = -1

while ret:
    ret, frame = cap.read()
    frame_number += 1
    if ret and frame_number < 10: 
        # Detect Vehicles
        detections = coco_model(frame)[0]
        # print(detections) # Este se utiliza para que el programa este corriendo correctamente 
        for detection in detections.boxes.data.toList(): 
            print(detection)

        # Track Vehicles

        # Detect License Plates 

        # Assing License Plate to Car

        # Crop License Plate 

        # Process License Plate