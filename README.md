# Hackaton
Código para la camara del semaforo del hackaton

# Utilizar el codigo en un Entorno Virtual
Se tiene que utilizar python 3.9 - 3.12 (Yo utilice 3.12)
Create a Virtual Environment:
        python3.12 -m venv venv
    
Activate the Virtual Environment:
        source venv/bin/activate

Install OpenCV in the Virtual Environment:
        pip install opencv-python

Run Your Script: While the virtual environment is active, run your script:
        python camarav1.py

Deactivate the Virtual Environment: When you're done, deactivate the virtual environment:
        deactivate

# Instalaciones
(Se descarga PyTorch para poder utilizar ultranalytics, esto es de mac)
    pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu

(Se descarga ultralytics para usar YOLO)
    pip install ultralytics

(Se descarga opencv para el uso de la camara)
    pip install opencv-python

(Se usa para el modulo del archivo sort)
    pip install scikit-image
    pip install filterpy

(Se usa para el archivo util)
pip install easyocr

# Comentarios
Darle prioridad a ambulancias patrullas o policias con sirenas activas