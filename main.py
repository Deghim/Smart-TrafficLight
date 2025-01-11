from ultralytics import YOLO # type: ignore
import cv2 # libreria utilizada para uso de imagenes

from sort.sort import *
import numpy as np  
from util import *

# Load Models
coco_model = YOLO('yolov8n.pt') # Modelo de YOLO ya entrenado usado para detectar carros
license_plate_detector  = YOLO('license_plate_detector.pt' ) # Se crea un modelo que detecta placas

# Load Videos
# cap = cv2.VideoCapture('testvideos/27260-362770008_tiny.mp4') # Se utiliza un video para testear el modelo
cap = cv2.VideoCapture('testvideos/8132-207209040_tiny.mp4') # Se utiliza un video para testear el modelo

# Model Variables 
vehicles = [2,3,5,6,7] # Aqui se almacenan las id's de las clases pertenecientes de la clase vehiculos del dataset de coco
mot_tracker = Sort() # Objeto para trackear a los vehiculos
results = {}

# Read Frames
ret = True # Detecta que se hayan leido los frames
frame_number = -1

while ret:
    ret, frame = cap.read()
    frame_number += 1
    if ret and frame_number < 10: 
        results[frame_number] = {}
        # Detect Vehicles
        detections = coco_model(frame)[0]
        detections_ = [] # Almacenamiento del perimetro de las cajas de los vehiculos
        # print(detections) # Este se utiliza para que el programa este corriendo correctamente 
        for detection in detections.boxes.data.tolist(): 
            # print(detection)
            x1, y1, x2, y2, score, class_id = detection
            if int(class_id) in vehicles:
                detections_.append([x1, y1, x2, y2, score])

        # Track Vehicles
        """
        Se utiliza para trackear los vehiculos
        """
        track_ids = mot_tracker.update(np.asarray(detections_))

        # Detect License Plates  min 21:00 
        license_plates = license_plate_detector(frame)[0]
        for license_plate in license_plates.boxes.data.tolist(): 
            x1, y1, x2, y2, score, class_id = license_plate

            # Assing License Plate to Car
            xcar1,ycar1, xcar2, ycar2, car_id = get_car(license_plate, track_ids)

            # Crop License Plate 
            license_plate_crop = frame[int(y1): int(y2), int(x1): int(x2) :]

            # Process License Plate
            license_plate_crop_gray = cv2.cvtColor(license_plate_crop, cv2.COLOR_BGR2GRAY)
            _, license_plate_crop_threshold = cv2.threshold(license_plate_crop_gray, 64, 255, cv2.THRESH_BINARY_INV)

            # cv2.imshow('Original_crop', license_plate_crop)
            # cv2.imshow('Threshold', license_plate_crop_threshold)

            # cv2.waitKey(0)

            # Read License Plate number
            license_plate_text, license_plate_text_score = read_license_plate(license_plate_crop_threshold)

            if license_plate_text is not None:
                results[frame_number][car_id] = {'car': {"bbox":[xcar1,ycar1, xcar2, ycar2]}
                                                 , "license_plate":{"bbox":[x1, y1, x2, y2]
                                                                    ,"text": license_plate_text
                                                                    ,"bbox_score": score
                                                                    ,"text_score": license_plate_text_score}}
# Write Results
write_csv(results,'./test.csv')