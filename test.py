from ultralytics import YOLO

model = YOLO('yolov8n.pt')

results = model("27260-362770008_tiny.mp4",show=True,save=True)

"""
Test file para probar que funcione correctamente el modelo

Si funciona correctament, deberia de guardar un video nuevo con el reconocimento de los objetos
"""