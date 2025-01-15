from ultralytics import YOLO # type: ignore
import cv2

from util import *

""" Load Models"""
coco_model = YOLO('yolov8n.pt') # Modelo de YOLO ya entrenado usado para detectar carros
license_plate_detector  = YOLO('license_plate_detector.pt' ) # Se crea un modelo que detecta placas

""" Load Videos """
# cap = cv2.VideoCapture('testvideos/27260-362770008_tiny.mp4') # Se utiliza un video para testear el modelo
# stream = cv2.VideoCapture('testvideos/PeopleWalking.mp4') # Se utiliza un video para testear el modelo
# stream = cv2.VideoCapture('testvideos/Wondercamp.mp4')
# stream = cv2.VideoCapture('testvideos/trackVelocidad.mp4')
stream = cv2.VideoCapture('testvideos/SoloCar.mp4')
# stream = cv2.VideoCapture(0)

""" Model Variables """
vehicles = [2,3,5,6,7,8] # Aqui se almacenan las id's de las clases pertenecientes de la clase vehiculos del dataset de coco
civilians = [0,16,17] # Id's de posibles peatones
track_history = {}

def draw_detection_boxes(frame, detection):
    for result in detection:
        for box in result.boxes:
            cls_id = int(box.cls[0].item()) # Get class ID and check if it's a vehicle
            track_id = int(box.id[0].item()) if box.id is not None else -404  # Track ID

            # print(f"{cls_id}: {result.names[cls_id]}")

            if cls_id in vehicles:
                x1, y1, x2, y2 = map(int, box.xyxy[0].cpu().numpy()) # Extract coordinates and convert to integers
                confidence = float(box.conf[0].cpu().numpy())
                color = (255,255,255)
                
                cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2) # Draw rectangle around vehicle
                
                label = f"Id: {track_id}, {result.names[cls_id]}: {confidence:.2f} " # Add label with class name and confidence
                cv2.putText(frame, label, (x1, y1-10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)

                # print(f"{cls_id}: {result.names[cls_id]} - TrackID: {track_id}")
                
                
                if track_id not in track_history and confidence > 0.5: #Se asegura que la confianza de deteccion sea segura para agregar la informacion
                    track_history[track_id] = {
                        "classID": cls_id,
                        "class": result.names[cls_id],
                        "confidence":round(confidence, 2),
                        "velocidadMax": None,
                        "condicionesClimatologicas": None
                    } 
    
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

        frameWithBoxes = draw_detection_boxes(frame, frameDetected)

        cv2.imshow("Video Capture", frameWithBoxes)
        if cv2.waitKey(1) == ord('q'):
            break       

    print(track_history)
    stream.release()
    cv2.destroyAllWindows() #!

if __name__ == "__main__":
    video_stream()