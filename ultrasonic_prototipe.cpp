/**************************************************************
   Lógica de Semáforo + FastTrack + RFID (ESP32-S3)
   - RFID con pines SPI: SCK=12, MOSI=11, MISO=13, SS=10, RST=4
   - Semáforo dirección 0: r=35, y=36, g=37, trig=5, echo=14
 **************************************************************/

#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>

// ---------- [CONFIGURACIÓN RFID en ESP32-S3] ----------
#define SCK_PIN 12
#define MOSI_PIN 11
#define MISO_PIN 13
#define SS_PIN 10 // SDA del RC522
#define RST_PIN 4 // RST del RC522

SPIClass spiBus(HSPI);            // Bus SPI en ESP32-S3
MFRC522 mfrc522(SS_PIN, RST_PIN); // Lector RFID

// Para saber si hay tarjeta detectada (usado en Fast Track)
bool tarjetaDetectada = false;

// ---------- [CONFIGURACIÓN WiFi y Fast Track] ----------
const char *ssid = "Totalplay-86A5";
const char *password = "86A5ED567Wg8ReVT";
const char *apiURL = "http://192.168.100.52:3000/modo_fast_track/67d0e7f92509f8bd4991a88c";

// Modo FastTrack protegido con mutex
bool modoFastTrack = false;
SemaphoreHandle_t xMutex = NULL;

// Controlar estado de la pantalla
enum EstadoPantalla
{
  PIQUELE,
  ANIMACION_MONITO,
  ESPERE,
  FAST_TRACK
};

EstadoPantalla estadoPantallaActual = PIQUELE;
EstadoPantalla estadoPantallaAnterior = PIQUELE; // Para recordar el estado antes de Fast Track

// ---------- [CONFIGURACIÓN OLED] ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pines I2C OLED
const int OLED_SDA = 20;
const int OLED_SCL = 21;
// ---------- [CONFIGURACIÓN SEMÁFORO] ----------
const int NUM_DIRECCIONES = 4;
const int trigPins[NUM_DIRECCIONES] = {5, 47, 18, 16};
const int echoPins[NUM_DIRECCIONES] = {14, 48, 8, 17};
const int redLEDs[NUM_DIRECCIONES] = {1, 7, 35, 9};
const int greenLEDs[NUM_DIRECCIONES] = {2, 6, 37, 3};
const int amarilloLEDs[NUM_DIRECCIONES] = {42, 15, 36, 46};

// Botón peatonal
const int botonPeaton = 40;

// Constantes semáforo
const float DIST_UMBRAL = 12.0;
const TickType_t SEMAFORO_LOOP = 1000 / portTICK_PERIOD_MS;
const unsigned long TIEMPO_VERDE_MAX = 2000;
const unsigned long TIEMPO_AMARILLO_MAX = 100;
const unsigned long TIEMPO_SIN_CARRO = 2200;
const unsigned long TIEMPO_PEATON = 15000;
const unsigned long TIEMPO_ESPERA_BOTON = 100000;

// Cola de direcciones
int cola[NUM_DIRECCIONES];
int tamanoCola = 0;
int direccionActual = -1;
unsigned long tiempoInicioVerde = 0;
unsigned long tiempoUltimoCarroDetectado = 0;

// ---------- [HANDLES DE TAREAS] ----------
TaskHandle_t xHandleSemaforo = NULL;
TaskHandle_t xHandleBoton = NULL;
TaskHandle_t xHandleFastTrack = NULL;
TaskHandle_t xHandleRFID = NULL;

// ---------- [DECLARACIONES DE TAREAS] ----------
void TareaSemaforo(void *pvParameters);
void TareaBoton(void *pvParameters);
void TareaGetFastTrack(void *pvParameters);
void TareaRFID(void *pvParameters);

// Funciones auxiliares
float leerDistancia(int dir);
boolean estaEnCola(int dir);
void agregarACola(int dir);
void removerDeCola();
void iniciarLuzVerde(int dir);
void luzAmarilla(int dir);
void detenerLuzVerde(int dir);
void ponerTodoRojo();
unsigned long getTimeMs();

// Funciones OLED
void mostrarPantallaNormal();
void mostrarFastTrack(bool estado);
void mostrarAnimacionMonito();
void mostrarEspere();
void actualizarPantalla(EstadoPantalla estado);

// ------------------ [SETUP] ------------------
void setup()
{
  Serial.begin(115200);

  // 1) Iniciar bus SPI (FSPI) con pines
  spiBus.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);

  // 2) Inicializar lector RFID
  mfrc522.PCD_Init();
  Serial.println("MFRC522 inicializado con FSPI.");

  // 3) Conexión WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi...");
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20)
  {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi conectado!");
  }
  else
  {
    Serial.println("\nFallo en conexión WiFi. Continuando sin conectividad.");
  }

  // 4) Mutex para proteger modoFastTrack
  xMutex = xSemaphoreCreateMutex();

  // 5) Inicializar OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("Error al inicializar SSD1306"));
    while (1)
      ;
  }

  // Mostrar "PIQUELE" al inicio
  actualizarPantalla(PIQUELE);

  // 6) Configurar pines de LEDs y ultrasonidos
  for (int i = 0; i < NUM_DIRECCIONES; i++)
  {
    pinMode(trigPins[i], OUTPUT);
    pinMode(echoPins[i], INPUT);

    pinMode(redLEDs[i], OUTPUT);
    pinMode(greenLEDs[i], OUTPUT);
    pinMode(amarilloLEDs[i], OUTPUT);

    // Todos en rojo inicialmente
    digitalWrite(redLEDs[i], HIGH);
    digitalWrite(greenLEDs[i], LOW);
    digitalWrite(amarilloLEDs[i], LOW);
  }

  // 7) Inicializar cola
  for (int i = 0; i < NUM_DIRECCIONES; i++)
  {
    cola[i] = -1;
  }

  // 8) Botón peatonal
  pinMode(botonPeaton, INPUT_PULLUP);

  // 9) Crear tareas
  xTaskCreate(TareaSemaforo, "TareaSemaforo", 4096, NULL, 1, &xHandleSemaforo);
  xTaskCreate(TareaBoton, "TareaBoton", 2048, NULL, 1, &xHandleBoton);
  xTaskCreate(TareaGetFastTrack, "TareaGetFastTrack", 8192, NULL, 1, &xHandleFastTrack);
  xTaskCreate(TareaRFID, "TareaRFID", 4096, NULL, 1, &xHandleRFID);
}

void loop()
{
  // Vacío. Uso de tareas FreeRTOS
}

// ------------------ [TAREA SEMÁFORO] ------------------
void TareaSemaforo(void *pvParameters)
{
  for (;;)
  {
    unsigned long tiempoActual = getTimeMs();

    bool fastTrackActivo = false;
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
      fastTrackActivo = modoFastTrack;
      xSemaphoreGive(xMutex);
    }

    // Leer sensores y actualizar cola
    for (int i = 0; i < NUM_DIRECCIONES; i++)
    {
      float distancia = leerDistancia(i);
      if (distancia < DIST_UMBRAL && !estaEnCola(i))
      {
        agregarACola(i);
      }
    }

    // Lógica principal
    if (direccionActual == -1 && tamanoCola > 0)
    {
      direccionActual = cola[0];
      iniciarLuzVerde(direccionActual);
      tiempoInicioVerde = tiempoActual;
      tiempoUltimoCarroDetectado = tiempoActual;
    }

    if (direccionActual != -1)
    {
      unsigned long tiempoEnVerde = tiempoActual - tiempoInicioVerde;
      unsigned long tiempoSinCarro = tiempoActual - tiempoUltimoCarroDetectado;
      int n = 0;
      for (int a = 0; a < tamanoCola; a++)
      {
        if (cola[a] != -1)
          n++;
      }

      unsigned long tiempoVerdeMaxActual = TIEMPO_VERDE_MAX;
      if (fastTrackActivo)
      {
        // Modo fast track: reducir tiempo verde
        tiempoVerdeMaxActual = TIEMPO_VERDE_MAX / 2;
      }

      if ((tiempoEnVerde >= tiempoVerdeMaxActual) && n > 1)
      {
        detenerLuzVerde(direccionActual);
        removerDeCola();
        direccionActual = -1;
      }
      else
      {
        float distActual = leerDistancia(direccionActual);
        if (distActual < DIST_UMBRAL)
        {
          tiempoUltimoCarroDetectado = tiempoActual;
        }
        else
        {
          if (tiempoSinCarro >= TIEMPO_SIN_CARRO)
          {
            detenerLuzVerde(direccionActual);
            removerDeCola();
            direccionActual = -1;
          }
        }
      }
    }

    if (tamanoCola == 0 && direccionActual == -1)
    {
      ponerTodoRojo();
    }

    vTaskDelay(SEMAFORO_LOOP);
  }
}

// ------------------ [TAREA BOTÓN PEATONAL] ------------------
void TareaBoton(void *pvParameters)
{
  for (;;)
  {
    if (digitalRead(botonPeaton) == LOW)
    {
      vTaskDelay((TIEMPO_PEATON / 3) / portTICK_PERIOD_MS);
      Serial.println("PASO EL TIME, MODO PEATON INICIANDING");

      // Cambiar pantalla a animación de monito al presionar el botón
      actualizarPantalla(ANIMACION_MONITO);

      // Suspender semáforo
      vTaskSuspend(xHandleSemaforo);
      Serial.println("SEMAFORO OFF");

      // Animación de apagado si hay verde
      for (int i = 0; i < NUM_DIRECCIONES; i++)
      {
        if (digitalRead(greenLEDs[i]) == HIGH)
        {
          digitalWrite(greenLEDs[i], LOW);
          luzAmarilla(i);
        }
      }
      ponerTodoRojo();
      Serial.println("Cruce peatonal 15s...");

      // Ahora el mostrarAnimacionMonito() equivale a la función anterior activarModoPeaton()
      mostrarAnimacionMonito();

      // Resetear semáforo
      direccionActual = -1;
      tamanoCola = 0;
      for (int i = 0; i < NUM_DIRECCIONES; i++)
      {
        cola[i] = -1;
      }
      tiempoInicioVerde = 0;
      tiempoUltimoCarroDetectado = 0;

      // Reanudar semáforo
      vTaskResume(xHandleSemaforo);
      Serial.println("Fin de cruce peatonal. Reanudando semáforo.");

      // Cambiar pantalla a ESPERE
      actualizarPantalla(ESPERE);

      vTaskDelay((TIEMPO_ESPERA_BOTON) / portTICK_PERIOD_MS);
      Serial.println("boton disponible");

      // Volver a PIQUELE después del tiempo de espera
      actualizarPantalla(PIQUELE);
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// ------------------ [TAREA GET FAST TRACK] ------------------
void TareaGetFastTrack(void *pvParameters)
{
  vTaskDelay(5000 / portTICK_PERIOD_MS); // Espera WiFi

  bool estadoAnterior = false;

  for (;;)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      HTTPClient http;
      http.begin(apiURL);

      int httpCode = http.GET();
      if (httpCode == 200)
      {
        String payload = http.getString();
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error)
        {
          bool nuevoEstado = doc["modo_fast_track"];

          if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
          {
            modoFastTrack = nuevoEstado;
            xSemaphoreGive(xMutex);
          }

          if (nuevoEstado != estadoAnterior)
          {
            Serial.print("Cambio modo_fast_track a: ");
            Serial.println(nuevoEstado ? "true" : "false");

            // Guardar estado anterior de la pantalla antes de cambiar a Fast Track
            if (nuevoEstado)
            {
              estadoPantallaAnterior = estadoPantallaActual;
              actualizarPantalla(FAST_TRACK);
            }

            estadoAnterior = nuevoEstado;

            if (nuevoEstado)
            {
              // FAST TRACK ON
              // 1) Suspender semáforo y botón
              vTaskSuspend(xHandleSemaforo);
              vTaskSuspend(xHandleBoton);

              // 2) Reanudar TareaRFID
              vTaskResume(xHandleRFID);

              // 3) Ya se mostró "FAST TRACK" con actualizarPantalla
              // 4) Parpadeo rojo/verde en bucle hasta tarjeta
              Serial.println("Iniciando parpadeo FAST TRACK, esperando tarjeta...");
              while (true)
              {
                bool tarjetaLeida = false;
                if (xSemaphoreTake(xMutex, 0) == pdTRUE)
                {
                  tarjetaLeida = tarjetaDetectada;
                  xSemaphoreGive(xMutex);
                }
                if (tarjetaLeida)
                {
                  Serial.println("Tarjeta detectada, saliendo del parpadeo.");
                  break;
                }

                // Parpadeo: rojo -> verde
                for (int i = 0; i < NUM_DIRECCIONES; i++)
                {
                  digitalWrite(redLEDs[i], LOW);
                  digitalWrite(greenLEDs[i], HIGH);
                }
                vTaskDelay(500 / portTICK_PERIOD_MS);

                // Parpadeo: verde -> rojo
                for (int i = 0; i < NUM_DIRECCIONES; i++)
                {
                  digitalWrite(greenLEDs[i], LOW);
                  digitalWrite(redLEDs[i], HIGH);
                }
                vTaskDelay(500 / portTICK_PERIOD_MS);
              }

              // 5) Tarjeta detectada => dir[0] verde 20s
              ponerTodoRojo();
              digitalWrite(greenLEDs[0], HIGH);
              Serial.println("Direccion[0] en verde por 20s...");
              vTaskDelay(20000 / portTICK_PERIOD_MS);

              // 6) Volver a la normalidad
              tarjetaDetectada = false;
              ponerTodoRojo();
              vTaskResume(xHandleSemaforo);
              vTaskResume(xHandleBoton);

              // Desactivar fast track
              if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
              {
                modoFastTrack = false;
                xSemaphoreGive(xMutex);
              }
              estadoAnterior = false;
              Serial.println("Fin de Fast Track. Modo normal.");

              // Volver al estado anterior de la pantalla
              actualizarPantalla(estadoPantallaAnterior);
            }
            else
            {
              // FAST TRACK OFF
              vTaskSuspend(xHandleRFID);
              Serial.println("Tarea RFID suspendida");
              vTaskResume(xHandleSemaforo);
              vTaskResume(xHandleBoton);
              Serial.println("Fast Track OFF -> Semáforo y Botón reanudados.");

              // Volver al estado anterior de la pantalla
              actualizarPantalla(estadoPantallaAnterior);
            }
          }
        }
        else
        {
          Serial.println("Error al deserializar JSON");
        }
      }
      else
      {
        Serial.print("Error en solicitud GET: ");
        Serial.println(httpCode);
      }
      http.end();
    }
    else
    {
      Serial.println("Desconectado del WiFi. Intentando reconectar...");
      WiFi.reconnect();
    }
    vTaskDelay(3000 / portTICK_PERIOD_MS);
  }
}

// ------------------ [TAREA RFID] ------------------
void TareaRFID(void *pvParameters)
{
  // Suspendida al inicio, se activa con fast track
  vTaskSuspend(NULL);

  for (;;)
  {
    if (mfrc522.PICC_IsNewCardPresent())
    {
      if (mfrc522.PICC_ReadCardSerial())
      {
        Serial.println("Tarjeta RFID detectada!");

        // UID (debug)
        String content;
        for (byte i = 0; i < mfrc522.uid.size; i++)
        {
          content += (mfrc522.uid.uidByte[i] < 0x10) ? " 0" : " ";
          content += String(mfrc522.uid.uidByte[i], HEX);
        }
        content.toUpperCase();
        Serial.println("ID de tarjeta: " + content);

        // Indicar que se detectó tarjeta
        if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
        {
          tarjetaDetectada = true;
          xSemaphoreGive(xMutex);
        }

        // Esperar 1s para evitar lecturas repetidas
        vTaskDelay(1000 / portTICK_PERIOD_MS);
      }
    }
    else
    {
      // Si no hay tarjeta, ponemos false
      if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
      {
        tarjetaDetectada = false;
        xSemaphoreGive(xMutex);
      }
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// ------------------ [FUNCIONES OLED] ------------------
// Función central para actualizar la pantalla basada en estado
void actualizarPantalla(EstadoPantalla estado)
{
  estadoPantallaActual = estado;

  switch (estado)
  {
  case PIQUELE:
    mostrarPantallaNormal();
    break;
  case ANIMACION_MONITO:
    // La animación se maneja en mostrarAnimacionMonito()
    break;
  case ESPERE:
    mostrarEspere();
    break;
  case FAST_TRACK:
    mostrarFastTrack(true);
    break;
  }
}

void mostrarPantallaNormal()
{
  display.clearDisplay();
  display.setTextSize(2.9);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(25, 30);
  display.println("PIQUELE");
  display.display();
}

void mostrarFastTrack(bool estado)
{
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(5, 10);
  display.println("FAST TRACK");

  display.setTextSize(3);
  display.setCursor(estado ? 40 : 20, 40);
  display.println(estado ? "ON" : "OFF");
  display.display();
}

// Función para mostrar la animación del monito (antes era activarModoPeaton)
void mostrarAnimacionMonito()
{
  int posX = 0;
  for (int segundos = TIEMPO_PEATON / 1000; segundos > 0; segundos--)
  {
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(70, 10);
    display.print(segundos);

    display.setTextSize(2);
    display.setCursor(60, 45);
    display.println("segs");

    // Monito animado
    display.fillCircle(posX + 10, 25, 5, SSD1306_WHITE);
    display.drawLine(posX + 10, 30, posX + 10, 40, SSD1306_WHITE);

    if (segundos % 2 == 0)
    {
      display.drawLine(posX + 10, 33, posX + 5, 28, SSD1306_WHITE);
      display.drawLine(posX + 10, 33, posX + 15, 38, SSD1306_WHITE);
    }
    else
    {
      display.drawLine(posX + 10, 33, posX + 5, 38, SSD1306_WHITE);
      display.drawLine(posX + 10, 33, posX + 15, 28, SSD1306_WHITE);
    }

    if (segundos % 2 == 0)
    {
      display.drawLine(posX + 10, 40, posX + 5, 48, SSD1306_WHITE);
      display.drawLine(posX + 10, 40, posX + 15, 48, SSD1306_WHITE);
    }
    else
    {
      display.drawLine(posX + 10, 40, posX + 5, 45, SSD1306_WHITE);
      display.drawLine(posX + 10, 40, posX + 15, 45, SSD1306_WHITE);
    }

    display.display();
    posX += 4;
    if (posX > 100)
      posX = 0;
    delay(1000);
  }
}

void mostrarEspere()
{
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(12, 18);
  display.println("ESPERE");
  display.display();
}

// ------------------ [AUXILIARES SEMÁFORO] ------------------
float leerDistancia(int dir)
{
  digitalWrite(trigPins[dir], LOW);
  delayMicroseconds(2);
  digitalWrite(trigPins[dir], HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPins[dir], LOW);

  long duracion = pulseIn(echoPins[dir], HIGH, 30000);
  if (duracion == 0)
  {
    return 1000.0;
  }
  float distancia = (duracion * 0.034) / 2;
  return distancia;
}

boolean estaEnCola(int dir)
{
  for (int i = 0; i < tamanoCola; i++)
  {
    if (cola[i] == dir)
      return true;
  }
  return false;
}

void agregarACola(int dir)
{
  if (tamanoCola < NUM_DIRECCIONES)
  {
    cola[tamanoCola] = dir;
    tamanoCola++;
  }
}

void removerDeCola()
{
  if (tamanoCola > 0)
  {
    for (int i = 0; i < (tamanoCola - 1); i++)
    {
      cola[i] = cola[i + 1];
    }
    cola[tamanoCola - 1] = -1;
    tamanoCola--;
  }
}

void iniciarLuzVerde(int dir)
{
  digitalWrite(redLEDs[dir], LOW);
  digitalWrite(greenLEDs[dir], HIGH);
  for (int i = 0; i < NUM_DIRECCIONES; i++)
  {
    if (i != dir)
    {
      digitalWrite(redLEDs[i], HIGH);
      digitalWrite(greenLEDs[i], LOW);
      digitalWrite(amarilloLEDs[i], LOW);
    }
  }
}

void luzAmarilla(int dir)
{
  for (int i = 0; i < 5; i++)
  {
    digitalWrite(greenLEDs[dir], HIGH);
    vTaskDelay(TIEMPO_AMARILLO_MAX / portTICK_PERIOD_MS);
    digitalWrite(greenLEDs[dir], LOW);
    vTaskDelay((TIEMPO_AMARILLO_MAX / 2) / portTICK_PERIOD_MS);
  }
  digitalWrite(amarilloLEDs[dir], HIGH);
  vTaskDelay((TIEMPO_AMARILLO_MAX / 2) / portTICK_PERIOD_MS);
  digitalWrite(amarilloLEDs[dir], LOW);
}

void detenerLuzVerde(int dir)
{
  digitalWrite(greenLEDs[dir], LOW);
  luzAmarilla(dir);
  digitalWrite(redLEDs[dir], HIGH);
}

void ponerTodoRojo()
{
  for (int i = 0; i < NUM_DIRECCIONES; i++)
  {
    digitalWrite(redLEDs[i], HIGH);
    digitalWrite(greenLEDs[i], LOW);
    digitalWrite(amarilloLEDs[i], LOW);
  }
}

unsigned long getTimeMs()
{
  return (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}