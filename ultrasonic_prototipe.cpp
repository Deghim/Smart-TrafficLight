/**
 * 
 * - TareaSemaforo: Controla la lógica de cola y LEDs según ultrasonidos.
 * - TareaBoton: Escucha el botón peatonal; si se presiona,
 *               suspende TareaSemaforo, hace animación y pone todo rojo,
 *               espera 15s y reanuda TareaSemaforo.
 * 
 * 
 * Para evitar bloqueos en TareaSemaforo, usamos vTaskDelay() en lugar de delay().
 */

#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Definición de tamaño de pantalla OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  

// Creación del objeto de pantalla
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Definición de pines I2C para OLED
const int OLED_SDA = 20;
const int OLED_SCL = 21;

// Pantalla normal mostrada
bool pantallaNormalMostrada = false;
// CONFIGURACIÓN DE PINES
const int NUM_DIRECCIONES = 4;

const int trigPins[NUM_DIRECCIONES]    = {13,  5, 18, 16};  // Pines TRIG de ultrasonidos
const int echoPins[NUM_DIRECCIONES]    = {14,  4,  8, 17};  // Pines ECHO de ultrasonidos
const int redLEDs[NUM_DIRECCIONES]     = { 1,  7, 10,  9};  // LEDs ROJOS
const int greenLEDs[NUM_DIRECCIONES]   = {42,  6, 12,  3};  // LEDs VERDES
const int amarilloLEDs[NUM_DIRECCIONES] = { 2, 15, 11, 46}; // LEDs AMARILLOS

const int botonPeaton = 40; 
// CONSTANTES DE TIEMPO
const float  DIST_UMBRAL       = 12.0;             // Umbral de detección (cm)
const TickType_t SEMAFORO_LOOP = 1000 / portTICK_PERIOD_MS; 
  // Cada 1000 ms (1s) se vuelve a ejecutar la lógica

const unsigned long TIEMPO_VERDE_MAX    = 2000;  // Máximo en verde (ms)
const unsigned long TIEMPO_AMARILLO_MAX = 100;  
const unsigned long TIEMPO_SIN_CARRO    = 2200;  // Considerar flujo detenido (ms)
const unsigned long TIEMPO_PEATON       = 15000; // 15s peatonal
const unsigned long TIEMPO_ESPERA_BOTON = 100000; // 100s espera entre usos del botón

// MANEJO DE COLAS
int cola[NUM_DIRECCIONES];
int tamanoCola = 0;

// -1 significa "ninguna dirección en verde"
int direccionActual = -1; 

// Variables para control de tiempos (en milisegundos)
unsigned long tiempoInicioVerde          = 0; 
unsigned long tiempoUltimoCarroDetectado = 0;
// HANDLES DE TAREAS
TaskHandle_t xHandleSemaforo = NULL;  // Tarea Semáforo
TaskHandle_t xHandleBoton    = NULL;  // Tarea Botón
// DECLARACIONES
void TareaSemaforo(void *pvParameters);
void TareaBoton(void *pvParameters);
// Funciones auxiliares
float   leerDistancia(int dir);
boolean estaEnCola(int dir);
void    agregarACola(int dir);
void    removerDeCola();
void    iniciarLuzVerde(int dir);
void    luzAmarilla(int dir);
void    detenerLuzVerde(int dir);
void    ponerTodoRojo();
unsigned long getTimeMs(); // Helper para leer "tiempo actual" en ms usando FreeRTOS

// Funciones para la pantalla OLED
void mostrarPantallaNormal();
void activarModoPeaton();
void desactivarModoPeaton();
void mostrarTiempoEspera(unsigned long tiempoRestante);
void setup() {
  Serial.begin(115200);
  
  // Configuración de pines I2C para OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  
  // Inicialización de la pantalla OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Dirección 0x3C para pantallas de 128x64
    Serial.println(F("Error al inicializar SSD1306"));
    while(1); // No continuar si falla
  }
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println("Semaforo Listo");
  display.display();
  delay(1000);
  
  // Mostrar "PIQUELE" al inicio
  mostrarPantallaNormal();
  
  // Configurar pines de LEDs y Ultrasonidos
  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    pinMode(trigPins[i], OUTPUT);
    pinMode(echoPins[i], INPUT);

    pinMode(redLEDs[i], OUTPUT);
    pinMode(greenLEDs[i], OUTPUT);
    pinMode(amarilloLEDs[i], OUTPUT);

    // Iniciar todos en rojo
    digitalWrite(redLEDs[i], HIGH);
    digitalWrite(greenLEDs[i], LOW);
    digitalWrite(amarilloLEDs[i], LOW);
  }

  // Inicializar cola de direcciones
  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    cola[i] = -1;
  }

  // Configurar botón peatonal
  pinMode(botonPeaton, INPUT_PULLUP); 
  // Nota: ajusta si usas pull-up/pull-down según tu hardware

  // Crear la TareaSemaforo
  xTaskCreate(
    TareaSemaforo,         // Función que implementa la tarea
    "TareaSemaforo",       // Nombre de la tarea (para depuración)
    4096,                  // Tamaño de la pila (bytes)
    NULL,                  // Parámetro (no usamos)
    1,                     // Prioridad (1 = baja; mayor = más alta)
    &xHandleSemaforo       // Guardamos el 'handle' en xHandleSemaforo
  );

  // Crear la TareaBoton
  xTaskCreate(
    TareaBoton,
    "TareaBoton",
    2048,
    NULL,
    1,
    &xHandleBoton
  );
}
void loop() {
}
// TAREA SEMÁFORO
void TareaSemaforo(void *pvParameters) {
  // Esta tarea controla la lógica completa del semáforo.
  // Se repite cada ~1 segundo (SEMAFORO_LOOP).

  for (;;) {
    unsigned long tiempoActual = getTimeMs(); // ms basado en ticks de FreeRTOS

    // 1) Leer sensores y actualizar cola
    for (int i = 0; i < NUM_DIRECCIONES; i++) {
      float distancia = leerDistancia(i);
      if (distancia < DIST_UMBRAL && !estaEnCola(i)) {
        agregarACola(i);
      }
    }

    // 2) Decidir estado
    // Si no hay dirección actual y la cola no está vacía...
    if (direccionActual == -1 && tamanoCola > 0) {
      direccionActual = cola[0];
      iniciarLuzVerde(direccionActual);
      tiempoInicioVerde          = tiempoActual;
      tiempoUltimoCarroDetectado = tiempoActual;
    }

    // - Si hay dirección actual en verde
    if (direccionActual != -1) {
      unsigned long tiempoEnVerde = tiempoActual - tiempoInicioVerde;
      unsigned long tiempoSinCarro = tiempoActual - tiempoUltimoCarroDetectado;
      int n = 0;
      for (int a = 0; a < tamanoCola; a++) {
        if (cola[a] != -1) n++;
      }
      // Revisar si se sobrepasó TIEMPO_VERDE_MAX
      if ((tiempoEnVerde >= TIEMPO_VERDE_MAX) && n > 1 ) {
        detenerLuzVerde(direccionActual);
        removerDeCola(); 
        direccionActual = -1;
      }
      else {
        // Comprobar si ya no hay vehículos en la dirección actual
        float distanciaDirActual = leerDistancia(direccionActual);
        if (distanciaDirActual < DIST_UMBRAL) {
          tiempoUltimoCarroDetectado = tiempoActual;
        } 
        else {
          // Si pasó TIEMPO_SIN_CARRO sin detección, acabamos verde
          if (tiempoSinCarro >= TIEMPO_SIN_CARRO) {
            detenerLuzVerde(direccionActual);
            removerDeCola();
            direccionActual = -1;
          }
        }
      }
    }

    // 3) Si la cola está vacía y no hay dirección actual => todo en rojo
    if (tamanoCola == 0 && direccionActual == -1) {
      ponerTodoRojo();
    }

    // Esperamos un segundo antes de la siguiente iteración
    vTaskDelay(SEMAFORO_LOOP);
  }
}
// TAREA BOTÓN PEATONAL
void TareaBoton(void *pvParameters) {
  for (;;) {
    if (digitalRead(botonPeaton) == LOW) {
      vTaskDelay((TIEMPO_PEATON/3) / portTICK_PERIOD_MS);
      Serial.println("PASO EL TIME, MODO PEATON INICIANDING");
      
      // Suspendemos la tarea del semáforo
      vTaskSuspend(xHandleSemaforo);
      Serial.println("SEMAFORO OFF");

      // Animación de apagado si alguna dirección está en verde
      for (int i = 0; i < NUM_DIRECCIONES; i++) {
        if (digitalRead(greenLEDs[i]) == HIGH) {
          digitalWrite(greenLEDs[i], LOW);
          luzAmarilla(i);
        }
      }

      // Poner todo en rojo
      ponerTodoRojo();

      Serial.println("Cruce peatonal 15s...");
      
      // Activar modo peatón con contador
      activarModoPeaton();

      // AQUÍ: Resetear las variables de estado del semáforo antes de reanudar
      direccionActual = -1;  // No hay dirección actual
      
      // Resetear la cola y su tamaño
      tamanoCola = 0;
      for (int i = 0; i < NUM_DIRECCIONES; i++) {
        cola[i] = -1;
      }
      
      // Resetear los tiempos
      tiempoInicioVerde = 0;
      tiempoUltimoCarroDetectado = 0;

      // Ahora reanudamos la tarea del semáforo
      vTaskResume(xHandleSemaforo);
      Serial.println("Fin de cruce peatonal. Reanudando semáforo con estado limpio.");
      
      // Mostrar mensaje de espera
      mostrarEspera();
      
      vTaskDelay((TIEMPO_ESPERA_BOTON) / portTICK_PERIOD_MS);
      Serial.println("boton disponible");
      
      // Volver a mostrar "PIQUELE" cuando el botón está disponible nuevamente
      mostrarPantallaNormal();
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}
// FUNCIONES OLED
// Mostrar la pantalla normal del semáforo ("piquele")
void mostrarPantallaNormal() {
  display.clearDisplay();
  display.setTextSize(2.9);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(25, 30);
  display.println("PIQUELE");
  display.display();
  pantallaNormalMostrada = true;
}
void activarModoPeaton() {
  int posX = 0;
  
  // Se mostrará un contador de tiempo restante con animación de mono
  for (int segundos = TIEMPO_PEATON/1000; segundos > 0; segundos--) {
    display.clearDisplay();
    
    // Dibujar el contador de segundos
    display.setTextSize(3);
    display.setCursor(70, 10);
    display.print(segundos);
    
    display.setTextSize(2);
    display.setCursor(60, 45);
    display.println("segs");
    
    // Dibujar un mono sencillo con primitivas gráficas
    // Cabeza - agregamos el parámetro de color SSD1306_WHITE
    display.fillCircle(posX + 10, 25, 5, SSD1306_WHITE);
    
    // Cuerpo
    display.drawLine(posX + 10, 30, posX + 10, 40, SSD1306_WHITE);
    
    // Brazos - alternar posición para dar impresión de movimiento
    if (segundos % 2 == 0) {
      // Brazo izquierdo arriba, derecho abajo
      display.drawLine(posX + 10, 33, posX + 5, 28, SSD1306_WHITE);
      display.drawLine(posX + 10, 33, posX + 15, 38, SSD1306_WHITE);
    } else {
      // Brazo izquierdo abajo, derecho arriba
      display.drawLine(posX + 10, 33, posX + 5, 38, SSD1306_WHITE);
      display.drawLine(posX + 10, 33, posX + 15, 28, SSD1306_WHITE);
    }
    
    // Piernas - alternar posición para simular pasos
    if (segundos % 2 == 0) {
      // Pierna izquierda adelante, derecha atrás
      display.drawLine(posX + 10, 40, posX + 5, 48, SSD1306_WHITE);
      display.drawLine(posX + 10, 40, posX + 15, 48, SSD1306_WHITE);
    } else {
      // Pierna izquierda atrás, derecha adelante
      display.drawLine(posX + 10, 40, posX + 5, 45, SSD1306_WHITE);
      display.drawLine(posX + 10, 40, posX + 15, 45, SSD1306_WHITE);
    }
    
    display.display();
    
    // Mover el mono para la siguiente iteración
    posX += 4;
    
    // Reiniciar posición cuando llega al borde
    if (posX > 100) {
      posX = 0;
    }
    
    delay(1000);
  }
}
// Mostrar mensaje de espera cuando el botón no está disponible
void mostrarEspera() {
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(12, 18);
  display.println("ESPERE");
  display.display();
}
// FUNCIONES AUXILIARES
float leerDistancia(int dir) {
  // Generar pulso trig
  digitalWrite(trigPins[dir], LOW);
  delayMicroseconds(2);
  digitalWrite(trigPins[dir], HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPins[dir], LOW);

  // Leer respuesta (con timeout de 30ms)
  long duracion = pulseIn(echoPins[dir], HIGH, 30000);
  if (duracion == 0) {
    return 1000.0; // No se detectó pulso => distancia "infinita"
  }
  // distancia en cm
  float distancia = (duracion * 0.034) / 2;
  return distancia;
}

boolean estaEnCola(int dir) {
  for (int i = 0; i < tamanoCola; i++) {
    if (cola[i] == dir) return true;
  }
  return false;
}

void agregarACola(int dir) {
  // Agrega dirección al final de la cola si hay espacio
  if (tamanoCola < NUM_DIRECCIONES) {
    cola[tamanoCola] = dir;
    tamanoCola++;
  }
}

void removerDeCola() {
  // Quita la 1ra dirección de la cola y desplaza todo
  if (tamanoCola > 0) {
    for (int i = 0; i < (tamanoCola - 1); i++) {
      cola[i] = cola[i + 1];
    }
    cola[tamanoCola - 1] = -1;
    tamanoCola--;
  }
}

void iniciarLuzVerde(int dir) {
  // Enciende verde en dir, apaga rojo
  digitalWrite(redLEDs[dir], LOW);
  digitalWrite(greenLEDs[dir], HIGH);

  // Asegura que las otras direcciones queden en rojo
  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    if (i != dir) {
      digitalWrite(redLEDs[i], HIGH);
      digitalWrite(greenLEDs[i], LOW);
      digitalWrite(amarilloLEDs[i], LOW);
    }
  }
}

void luzAmarilla(int dir) {
  // Pequeña animación en amarillo
  for (int i = 0; i < 5; i++) {
    digitalWrite(greenLEDs[dir], HIGH);
    vTaskDelay(TIEMPO_AMARILLO_MAX / portTICK_PERIOD_MS);
    digitalWrite(greenLEDs[dir], LOW);
    vTaskDelay((TIEMPO_AMARILLO_MAX / 2) / portTICK_PERIOD_MS);
  }
  digitalWrite(amarilloLEDs[dir],  HIGH);
  vTaskDelay((TIEMPO_AMARILLO_MAX / 2) / portTICK_PERIOD_MS);
  digitalWrite(amarilloLEDs[dir],  LOW);
}

void detenerLuzVerde(int dir) {
  // Apagar verde, animación amarillo, luego rojo
  digitalWrite(greenLEDs[dir], LOW);
  luzAmarilla(dir);
  digitalWrite(redLEDs[dir], HIGH);
}

void ponerTodoRojo() {
  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    digitalWrite(redLEDs[i], HIGH);
    digitalWrite(greenLEDs[i], LOW);
    digitalWrite(amarilloLEDs[i], LOW);
  }
}

// Helper para obtener tiempo en ms usando FreeRTOS
unsigned long getTimeMs() {
  return (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}
