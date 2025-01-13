from ultralytics import YOLO # type: ignore

model = YOLO('yolov8n.pt')
licenses = YOLO('license_plate_detector.pt')

results = model("testvideos/CaravanCouple.mp4",show=True,save=False)

# juan = licenses("testvideos/CaravanCouple.mp4",show=True,save=True)

"""
Test file para probar que funcione correctamente el modelo

Si funciona correctament, deberia de guardar un video nuevo con el reconocimento de los objetos
"""