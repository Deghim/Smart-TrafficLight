from ultralytics import YOLO # type: ignore
import cv2

import numpy as np
from util import *
import time
import os

""" Load Models"""
coco_model = YOLO('yolov8s.pt') # Modelo de YOLO ya entrenado usado para detectar carros
# license_plate_detector  = YOLO('license_plate_detector.pt' ) # Se crea un modelo que detecta placas

""" Load Videos """
# stream = cv2.VideoCapture('testvideos/testvideos/WhatsApp Image 2025-01-17 at 15.55.09.jpeg') # Se utiliza un video para testear el modelo
# stream = cv2.VideoCapture('testvideos/PeopleWalking.mp4') # Se utiliza un video para testear el modelo
# stream = cv2.VideoCapture('testvideos/Wondercamp.mp4')
stream = cv2.VideoCapture('testvideos/trackVelocidad.mp4')
# stream = cv2.VideoCapture('testvideos/CaravanCouple.mp4')
# stream = cv2.VideoCapture(0)

""" Model Variables """
vehicles = [2,3,5,6,7,8] # Aqui se almacenan las id's de las clases pertenecientes de la clase vehiculos del dataset de coco
civilians = [0,16,17] # Id's de posibles peatones
track_history = {}
pixel_to_meter_ratio = 0.05
last_update_time = time.time()

def send_to_database(data):
    os.system('clear')
    for track_id, info in data.items():
        print(f"Track ID: {track_id}, Data: {info}")

    print("Data sent successfully!\n")

def calculate_exposure(frame):
    """
    Calculate the darkness level of the frame.

    Args:
        frame (numpy.ndarray): Input video frame.

    Returns:
        str: Exposure level ("low", "medium", "high").
    """
    gray_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)  # Convert to grayscale
    mean_intensity = gray_frame.mean()  # Calculate mean brightness

    # Categorize exposure level
    if mean_intensity < 50:
        return {"low": mean_intensity}
    elif 50 <= mean_intensity < 150:
        return {"medium": mean_intensity}
    else:
        return {"high": mean_intensity}

def calculate_speed(prev, curr, fps, ratio):
    """
        Args:
        prev (tuple): Previous (x, y) position.
        curr (tuple): Current (x, y) position.
        fps (float): Frames per second.
        ratio (float): Meters per pixel ratio.
    """
    distance = np.sqrt((curr[0] - prev[0]) ** 2 + (curr[1] - prev[1]) ** 2) * ratio
    return distance * fps  # Speed in meters per second

def draw_detection_boxes(frame, detection, fps):
    exposure = calculate_exposure(frame)
    for result in detection:
        for box in result.boxes:
            cls_id = int(box.cls[0].item()) # Get class ID and check if it's a vehicle
            track_id = int(box.id[0].item()) if box.id is not None else -404  # Track ID

            # print(f"{cls_id}: {result.names[cls_id]}")

            if cls_id in vehicles:
                x1, y1, x2, y2 = map(int, box.xyxy[0].cpu().numpy()) # Extract coordinates and convert to integers
                confidence = float(box.conf[0].cpu().numpy())
                x_center = (x1 + x2) // 2
                y_center = (y1 + y2) // 2
                color = (255,255,255)
                
                cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2) # Draw rectangle around vehicle
                
                label = f"Id: {track_id}, {result.names[cls_id]}: {confidence:.2f} " # Add label with class name and confidence
                if track_id in track_history and "velocidadMax" in track_history[track_id]:
                    label += f", Max Speed: {track_history[track_id]['velocidadMax']:.2f} m/s"
                cv2.putText(frame, label, (x1, y1-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)

                # print(f"{cls_id}: {result.names[cls_id]} - TrackID: {track_id}")
                
                # Update track history
                if confidence > 0.5:
                    if track_id not in track_history:
                        track_history[track_id] = {
                            "classID": cls_id,
                            "class": result.names[cls_id],
                            "confidence": round(confidence, 2),
                            "velocidadMax": 0.0,
                            "exposure": exposure,  # Store exposure value
                            "positions": [(x_center, y_center)],
                        }
                    else:
                        track_history[track_id]["exposure"] = exposure
                        # Calculate speed
                        positions = track_history[track_id]["positions"]
                        positions.append((x_center, y_center))
                        if len(positions) > 1:
                            current_speed = calculate_speed(positions[-2], positions[-1], fps, pixel_to_meter_ratio)
                            track_history[track_id]["velocidadMax"] = max(
                                track_history[track_id]["velocidadMax"], current_speed
                            )
                        if len(positions) > 10:  # Limit position history to last 10 frames
                            positions.pop(0)
    
            if cls_id in civilians:
                x1, y1, x2, y2 = map(int, box.xyxy[0].cpu().numpy()) # Extract coordinates and convert to integers
                confidence = float(box.conf[0].cpu().numpy())
                color = (0,255,0)
                cv2.rectangle(frame, (x1, y1), (x2, y2),color, 2)# Draw rectangle around vehicle
                
                label = f"id: {cls_id}, {result.names[cls_id]}: {confidence:.2f} - ObjectID: {track_id}" # Add label with class name and confidence
                cv2.putText(frame, label, (x1, y1-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)

                if track_id not in track_history and confidence > 0.5: #Se asegura que la confianza de deteccion sea segura para agregar la informacion
                    track_history[track_id] = {
                        "class": result.names[cls_id],
                        "confidence":round(confidence, 2)
                    } 

    return frame

def video_stream():
    global last_update_time
    if not stream.isOpened():
        print("No stream :(")
        exit()

    fps = stream.get(cv2.CAP_PROP_FPS)
    width = int(stream.get(3))
    height = int(stream.get(4))

    while True:
        ret, frame = stream.read()
        if not ret:
            print("Stream Terminado")
            break

        # frameDetected = coco_model(frame, verbose=False) # verbose, se utiliza para escribir la informacion del frame en terminal

        frameDetected = coco_model.track(frame, persist=True, tracker="botsort.yaml", conf=0.3, iou=0.5, verbose=False)

        frameWithBoxes = draw_detection_boxes(frame, frameDetected, fps)

        cv2.imshow("Video Capture", frameWithBoxes)

        if time.time() - last_update_time >= 5:
            last_update_time = time.time()
            send_to_database(track_history)

        if cv2.waitKey(1) == ord('q'):
            break       

    stream.release()
    cv2.destroyAllWindows() #!

if __name__ == "__main__":
    video_stream()  