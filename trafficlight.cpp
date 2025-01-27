// Definición de pines para sensores y LEDs
const int NUM_DIRECCIONES = 4;

const int trigPins[NUM_DIRECCIONES] = {8, 6, 3, A3};
const int echoPins[NUM_DIRECCIONES] = {9, 7, 2, A2};

const int redLEDs[NUM_DIRECCIONES]   = {11, 4, 13, A5};
const int greenLEDs[NUM_DIRECCIONES] = {10, 5, 12, A4};
const int amarilloLEDs[NUM_DIRECCIONES] = { 0 ,1 ,A1 ,A0};

const float suelo = 12; // Distancia umbral en centímetros

const unsigned long TIEMPO_VERDE_MAX = 7000; // Tiempo máximo en verde (ms)
const unsigned long TIEMPO_AMARILLO_MAX = 1000; 
const unsigned long TIEMPO_SIN_CARRO = 1000;  // Tiempo para considerar que el flujo se detuvo (ms)

int cola[NUM_DIRECCIONES]; // Cola de direcciones
int tamanoCola = 0;

int direccionActual = -1; // Dirección actualmente en verde, -1 si ninguna

unsigned long tiempoInicioVerde = 0; // Momento en que se puso en verde
unsigned long tiempoUltimoCarroDetectado = 0; // Último momento en que se detectó un carro

unsigned long tiempoActual = 0;

void setup() {
  // Inicialización de la comunicación serial
  // Serial.begin(9600);

  // Configuración de pines
  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    pinMode(trigPins[i], OUTPUT);
    pinMode(echoPins[i], INPUT);
    pinMode(redLEDs[i], OUTPUT);
    pinMode(greenLEDs[i], OUTPUT);
    pinMode(amarilloLEDs[i], OUTPUT);
    

    // Inicializar LEDs en rojo
    digitalWrite(redLEDs[i], HIGH);
    digitalWrite(greenLEDs[i], LOW);
    digitalWrite(amarilloLEDs[i], LOW);
  }

  // Inicializar cola
  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    cola[i] = -1;
  }
}

void loop() {
  tiempoActual = millis();

  // Actualizar la cola basándose en la llegada de carros
  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    float distancia = leerDistancia(i);

    // Imprimir distancia medida
    // Serial.print("Direccion ");
    // Serial.print(i + 1);
    // Serial.print(": Distancia = ");
    // Serial.print(distancia);
    // Serial.println(" cm");

    // Si se detecta un carro y la dirección no está en la cola
    if (distancia < suelo && !estaEnCola(i)) {
      agregarACola(i);
      // Serial.print("Carro detectado en direccion ");
      // Serial.println(i + 1);
    }
  }

  // Si no hay dirección actual y la cola no está vacía, comenzar con la primera dirección en la cola
  if (direccionActual == -1 && tamanoCola > 0) {
    direccionActual = cola[0];
    iniciarLuzVerde(direccionActual);
    tiempoInicioVerde = tiempoActual;
    tiempoUltimoCarroDetectado = tiempoActual;

    // Serial.print("Iniciando luz verde en direccion ");
    // Serial.println(direccionActual + 1);
  }

  // Si hay una dirección siendo servida
  if (direccionActual != -1) {
    // Tiempo restante para el cambio o parada del semáforo
    unsigned long tiempoRestanteVerde = TIEMPO_VERDE_MAX - (tiempoActual - tiempoInicioVerde);
    unsigned long tiempoDesdeUltimoCarro = tiempoActual - tiempoUltimoCarroDetectado;

    // Serial.print("Direccion actual: ");
    // Serial.println(direccionActual + 1);
    // Serial.print("Tiempo restante en verde: ");
    // Serial.print(tiempoRestanteVerde / 1000.0, 2);
    // Serial.println(" s");

    // Verificar si se alcanzó el tiempo máximo en verde
    if (tiempoActual - tiempoInicioVerde >= TIEMPO_VERDE_MAX) {
      int n = 0;
      for(int a = 0 ; a < NUM_DIRECCIONES ; a++){
        if(cola[a] != -1)
          n++;
      }
      if(n!= 1)
        detenerLuzVerde(direccionActual);

      removerDeCola();
      // Serial.print("Tiempo máximo alcanzado en direccion ");
      // Serial.println(direccionActual + 1);
      direccionActual = -1;
    } else {
      // Verificar si el flujo ha parado
      float distancia = leerDistancia(direccionActual);

      if (distancia < suelo) {
        tiempoUltimoCarroDetectado = tiempoActual;
      } else {
        if (tiempoDesdeUltimoCarro >= TIEMPO_SIN_CARRO) {
          detenerLuzVerde(direccionActual);
          removerDeCola();
          // Serial.print("Flujo detenido en direccion ");
          // Serial.println(direccionActual + 1);
          direccionActual = -1;
        }
      }
    }
  }

  // Pequeña pausa para evitar lecturas demasiado frecuentes
  delay(1000);
}

float leerDistancia(int dir) {
  long duracion;
  float distancia;

  // Limpiar el pin trig
  digitalWrite(trigPins[dir], LOW);
  delayMicroseconds(2);

  // Enviar pulso de 10 microsegundos
  digitalWrite(trigPins[dir], HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPins[dir], LOW);

  // Leer el pulso de respuesta
  duracion = pulseIn(echoPins[dir], HIGH, 30000); // Tiempo de espera máximo de 30 ms

  // Calcular la distancia
  distancia = (duracion * 0.034) / 2;

  if (duracion == 0) {
    // No se recibió pulso (fuera de rango)
    distancia = 1000; // Asignar una distancia grande
  }

  return distancia;
}

boolean estaEnCola(int dir) {
  for (int i = 0; i < tamanoCola; i++) {
    if (cola[i] == dir) {
      return true;
    }
  }
  return false;
}

void agregarACola(int dir) {
  if (tamanoCola < NUM_DIRECCIONES) {
    cola[tamanoCola] = dir;
    tamanoCola++;
  }
}

void removerDeCola() {
  if (tamanoCola > 0) {
    for (int i = 0; i < tamanoCola - 1; i++) {
      cola[i] = cola[i + 1];
    }
    cola[tamanoCola - 1] = -1;
    tamanoCola--;
  }
}

void iniciarLuzVerde(int dir) {
  // Encender luz verde y apagar roja en la dirección actual
  digitalWrite(redLEDs[dir], LOW);
  digitalWrite(greenLEDs[dir], HIGH);

  // Asegurar que las otras direcciones estén en rojo
  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    if (i != dir) {
      digitalWrite(redLEDs[i], HIGH);
      digitalWrite(greenLEDs[i], LOW);
    }
  }
}
void luzAmarilla(int dir){
  for(int i=0 ; i<5;i++){
    digitalWrite(amarilloLEDs[dir], HIGH);
    delay(TIEMPO_AMARILLO_MAX);
    digitalWrite(amarilloLEDs[dir], LOW);
    delay(TIEMPO_AMARILLO_MAX/2);
  }
}
void detenerLuzVerde(int dir) {
  // Apagar luz verde y encender roja en la dirección actual
  digitalWrite(greenLEDs[dir], LOW);
  luzAmarilla(dir);
  digitalWrite(redLEDs[dir], HIGH);
}