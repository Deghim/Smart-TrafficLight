import cv2
import time
import numpy as np

from ultralytics import YOLO

# -----------------------------
# TRAFFIC LIGHT LOGIC CONSTANTS
# -----------------------------

NUM_DIRECCIONES = 4

# Maximum time (ms) that a direction can have the "green" light
TIEMPO_VERDE_MAX = 7000
# Time (ms) for the "yellow" light phase
TIEMPO_AMARILLO_MAX = 1000
# Time (ms) without new cars before we consider the flow to have stopped
TIEMPO_SIN_CARRO = 1000

# We'll store directions in a queue, just like the Arduino code
cola = [-1, -1, -1, -1]  # 4-slot queue
tamanoCola = 0

# Which direction currently has the green light?
direccionActual = -1

# Timestamps
tiempoInicioVerde = 0
tiempoUltimoCarroDetectado = 0
tiempoActual = 0  # Will update each loop

# -----------------------------------
# YOLO DETECTION & CAMERA VARIABLES
# -----------------------------------

# Example: We'll place an imaginary line dividing the frame into quadrants
# so that each quadrant corresponds to one of the 4 directions.
#   0 = top-left,    1 = top-right,
#   2 = bottom-left, 3 = bottom-right

# Adjust for your actual camera/video resolution
WIDTH = 1280
HEIGHT = 720

# YOLO model for detecting vehicles
coco_model = YOLO('yolov8s.pt')

# Video source
# stream = cv2.VideoCapture('testvideos/CaravanCouple.mp4')
stream = cv2.VideoCapture(0)

# Classes that we consider "vehicles" from COCO dataset: [car=2, motorcycle=3, bus=5, train=6, truck=7, boat=8]
vehicles = [2, 3, 5, 6, 7, 8]

# Track dictionary: each track_id stores:
# {
#   "direction": int (0..3),
#   "last_seen": float (timestamp of last detection),
#   ...
# }
track_info = {}

# --------------------------------
# HELPER / TRAFFIC LIGHT FUNCTIONS
# --------------------------------

def millis():
    """
    Returns the current time in milliseconds.
    This simulates Arduino's 'millis()' function.
    """
    return int(time.time() * 1000)

def estaEnCola(dir_):
    """
    Check if a given direction is already in the queue.
    """
    global tamanoCola, cola
    for i in range(tamanoCola):
        if cola[i] == dir_:
            return True
    return False

def agregarACola(dir_):
    """
    Add a direction to the queue if there's space.
    """
    global tamanoCola, cola
    if tamanoCola < NUM_DIRECCIONES:
        cola[tamanoCola] = dir_
        tamanoCola += 1

def removerDeCola():
    """
    Remove the first element from the queue (the direction that just finished).
    """
    global tamanoCola, cola
    if tamanoCola > 0:
        for i in range(tamanoCola - 1):
            cola[i] = cola[i + 1]
        cola[tamanoCola - 1] = -1
        tamanoCola -= 1

def iniciarLuzVerde(dir_):
    """
    Called when we set a direction to green. In actual hardware code, you'd set pins.
    Here we just note the direction is green.
    """
    # (Simulated) We can print or log that direction i is green
    print(f"[TrafficLight] Direction {dir_} is now GREEN.")

def luzAmarilla(dir_):
    """
    Called to simulate a short YELLOW light phase for the given direction before turning red.
    """
    print(f"[TrafficLight] Direction {dir_} is now YELLOW for {TIEMPO_AMARILLO_MAX} ms.")
    time.sleep(TIEMPO_AMARILLO_MAX / 1000.0)  # Sleep in seconds for demonstration

def detenerLuzVerde(dir_):
    """
    Called to turn off the green light for a direction.
    """
    print(f"[TrafficLight] Turning off GREEN for direction {dir_}.")
    luzAmarilla(dir_)  # do a short YELLOW phase
    print(f"[TrafficLight] Direction {dir_} is now RED.")

def determineDirection(x_center, y_center):
    """
    Determines which of the 4 directions (0..3) a bounding box center belongs to
    based on the image size.

    This is a simplified approach that splits the frame into quadrants:
      - 0: top-left
      - 1: top-right
      - 2: bottom-left
      - 3: bottom-right
    """
    # Adjust if needed to match your camera angle or intersection layout
    if y_center < HEIGHT // 2:
        # top
        if x_center < WIDTH // 2:
            return 0  # top-left
        else:
            return 1  # top-right
    else:
        # bottom
        if x_center < WIDTH // 2:
            return 2  # bottom-left
        else:
            return 3  # bottom-right

# -----------------------------
# MAIN LOOP & YOLO DETECTIONS
# -----------------------------

def main():
    global direccionActual
    global tiempoInicioVerde, tiempoUltimoCarroDetectado
    global tamanoCola, tiempoActual

    if not stream.isOpened():
        print("No stream :(")
        return

    fps = stream.get(cv2.CAP_PROP_FPS)
    if fps == 0:
        fps = 30  # fallback if video doesn't report fps

    start_time_ms = millis()

    while True:
        ret, frame = stream.read()
        if not ret:
            print("Stream finished or no frame.")
            break

        # Keep track of "now" in milliseconds for traffic light logic
        tiempoActual = millis()

        # 1. Perform YOLO detection
        #    We use track() to get IDs, so we can see which object is new or existing
        detections = coco_model.track(
            frame, persist=True, tracker="botsort.yaml", conf=0.3, iou=0.5, verbose=False
        )

        # 2. For each detection, figure out if it's a vehicle and in which direction
        for result in detections:
            for box in result.boxes:
                cls_id = int(box.cls[0].item())
                track_id = int(box.id[0].item()) if box.id is not None else -404
                if cls_id in vehicles:
                    # bounding box
                    x1, y1, x2, y2 = map(int, box.xyxy[0].cpu().numpy())
                    x_center = (x1 + x2) // 2
                    y_center = (y1 + y2) // 2

                    # Determine direction from bounding box center
                    direction = determineDirection(x_center, y_center)

                    # Mark that this track_id belongs to that direction
                    if track_id not in track_info:
                        track_info[track_id] = {
                            "direction": direction,
                            "arrival_time": tiempoActual  # first time we saw this ID
                        }
                        print(f"[DEBUG] Detected new vehicle (ID={track_id}) in direction {direction}.")
                    else:
                        # Update its "last seen" time (optional if needed)
                        track_info[track_id]["last_seen"] = tiempoActual

                    # If the direction is not in the queue, add it
                    #   This simulates the "car arrived" logic from the Arduino code
                    if not estaEnCola(direction):
                        agregarACola(direction)

        # 3. Traffic Light Logic
        #    - If there's no active direction, pick the first from the queue
        if direccionActual == -1 and tamanoCola > 0:
            direccionActual = cola[0]
            iniciarLuzVerde(direccionActual)
            tiempoInicioVerde = tiempoActual
            tiempoUltimoCarroDetectado = tiempoActual

        #    - If there's a direction currently green, check time constraints
        if direccionActual != -1:
            tiempoRestanteVerde = TIEMPO_VERDE_MAX - (tiempoActual - tiempoInicioVerde)
            tiempoDesdeUltimoCarro = tiempoActual - tiempoUltimoCarroDetectado

            # Check if we've exceeded the max green time
            if (tiempoActual - tiempoInicioVerde) >= TIEMPO_VERDE_MAX:
                print(f"[TrafficLight] Max green time reached for direction {direccionActual}.")
                detenerLuzVerde(direccionActual)
                removerDeCola()
                direccionActual = -1

            else:
                # Check if the flow in that direction has "stopped"
                # We'll do a simple check: if no new car has arrived in that direction
                # for TIEMPO_SIN_CARRO ms, we consider it empty.
                # In code #1, we read an ultrasonic sensor. 
                # Here, we approximate by seeing if a new track ID arrived or was updated.
                # We'll rely on 'tiempoUltimoCarroDetectado' to see if any new car was identified.
                
                # Has any car updated that direction recently?
                # We'll check track_info for any vehicle with that direction updated in the last TIEMPO_SIN_CARRO.
                
                direction_empty = True
                for tid, info in track_info.items():
                    if info["direction"] == direccionActual:
                        # If 'last_seen' is within TIEMPO_SIN_CARRO or arrival_time is within that window
                        last_seen_time = info.get("last_seen", info["arrival_time"])
                        if (tiempoActual - last_seen_time) < TIEMPO_SIN_CARRO:
                            direction_empty = False
                            tiempoUltimoCarroDetectado = tiempoActual
                            break

                if direction_empty:
                    print(f"[TrafficLight] No cars in direction {direccionActual} for {TIEMPO_SIN_CARRO} ms.")
                    detenerLuzVerde(direccionActual)
                    removerDeCola()
                    direccionActual = -1

        # Show the frame (optional visualization)
        cv2.imshow("Video Capture", frame)
        if cv2.waitKey(1) == ord('q'):
            break

    stream.release()
    cv2.destroyAllWindows()

    # At the end, we can see in which order directions were served
    print("Final directions queue state:", cola)
    print("Exiting...")

if __name__ == "__main__":
    main()
