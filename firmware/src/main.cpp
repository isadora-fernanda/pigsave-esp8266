#include <Arduino.h>
#include <Wire.h>
#include "config.h"

// A versão com Wi-Fi é escolhida no platformio.ini (ambiente "wifi").
// Sem ela, a cinta funciona no modo automático, só com o ciclo de tempo.
#ifdef PIGSAVE_WIFI
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

WiFiServer server(80);
String header = "";

IPAddress local_IP(192, 168, 1, FINAL_DO_IP);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

int final_do_ip = FINAL_DO_IP;
const char *nome = NOME_CINTA;
#endif

// Inclinação (graus) a partir da qual o ramo do lado é desligado
#define ANGLE 30

// Saídas para os MOSFETs de cada ramo de motores
#define LEFT 12
#define CENTER 14
#define RIGHT 16

// Entradas dos sensores
#define BAT A0
#define TEMP D8

#define POS D7

unsigned long oldTime = millis();
bool power = true;

const int MPU_addr = 0x68;
int16_t AcX, AcY, AcZ, Tmp, GyX, GyY, GyZ;

int minVal = 265;
int maxVal = 402;

double xAngle;
double x;
double yAngle;
double y;
double z;

unsigned short int Ton = TEMPO_LIGADO;
unsigned short int Toff = TEMPO_DESLIGADO;
double bateria = 0;

#ifdef PIGSAVE_WIFI
bool handleClient();
#endif

void setup() {
  // Acorda o MPU-6050 (registrador PWR_MGMT_1)
  Wire.begin();
  Wire.beginTransmission(MPU_addr);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);
  Serial.begin(9600);

  pinMode(LEFT, OUTPUT);
  pinMode(CENTER, OUTPUT);
  pinMode(RIGHT, OUTPUT);
  pinMode(BAT, INPUT);
  pinMode(TEMP, INPUT);

  pinMode(POS, OUTPUT);
  digitalWrite(LEFT, LOW);
  digitalWrite(CENTER, LOW);
  digitalWrite(RIGHT, LOW);

#ifdef PIGSAVE_WIFI
  WiFi.begin(ssid, password);
  WiFi.begin(ssid);
  oldTime = millis();
  if (!WiFi.config(local_IP, gateway, subnet))
  {
      Serial.println("STA Failed to configure");
  }
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.println(".");
    if (millis() > oldTime + 30000)  // espera a conexão com o Wi-Fi por até 30 s
      break;
  }
#endif

  // Pulso curto nos três ramos para indicar que a cinta ligou
  digitalWrite(LEFT, HIGH);
  digitalWrite(CENTER, HIGH);
  digitalWrite(RIGHT, HIGH);
  delay(500);
  digitalWrite(LEFT, LOW);
  digitalWrite(CENTER, LOW);
  digitalWrite(RIGHT, LOW);

#ifdef PIGSAVE_WIFI
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();
#endif
}

void loop() {
  bateria = analogRead(BAT);

  /*
  Mapeamento da bateria (tensão da bateria -> tensão no ADC -> leitura)

  10 V a 16 V   -> 0% a 100% -> 2,06 V a 3,3 V  -> 639 a 1024
  7,5 V a 12 V  -> 0% a 100% -> 1,55 V a 2,47 V -> 600 a 820
  */
  delay(1000);
  bateria = map(bateria, 600, 1024, 0, 100);
  if (bateria < 1)
    bateria = 0;
  else if (bateria > 100)
    bateria = 100;
  Serial.print("BATERIA= ");
  Serial.println(bateria);

  // Fora do tempo ligado, mede a inclinação da cinta (média de 50 leituras)
  int i = 0;
  if (!(millis() - oldTime < Ton) || !power) {
    xAngle = 0;
    for (i = 0; i < 50; i++) {
      digitalWrite(POS, LOW);
      digitalWrite(POS, HIGH);

      Wire.beginTransmission(MPU_addr);
      Wire.write(0x3B);
      Wire.requestFrom(MPU_addr, 14, true);

      AcX = Wire.read() << 8 | Wire.read();  // 0x3B (ACCEL_XOUT_H) & 0x3C (ACCEL_XOUT_L)
      AcY = Wire.read() << 8 | Wire.read();  // 0x3D (ACCEL_YOUT_H) & 0x3E (ACCEL_YOUT_L)
      AcZ = Wire.read() << 8 | Wire.read();  // 0x3F (ACCEL_ZOUT_H) & 0x40 (ACCEL_ZOUT_L)
      Tmp = Wire.read() << 8 | Wire.read();  // 0x41 (TEMP_OUT_H) & 0x42 (TEMP_OUT_L)
      GyX = Wire.read() << 8 | Wire.read();  // 0x43 (GYRO_XOUT_H) & 0x44 (GYRO_XOUT_L)
      GyY = Wire.read() << 8 | Wire.read();  // 0x45 (GYRO_YOUT_H) & 0x46 (GYRO_YOUT_L)
      GyZ = Wire.read() << 8 | Wire.read();  // 0x47 (GYRO_ZOUT_H) & 0x48 (GYRO_ZOUT_L)

      Wire.endTransmission(true);
      int xAng = map(AcX, minVal, maxVal, -90, 90);
      int yAng = map(AcY, minVal, maxVal, -90, 90);
      int zAng = map(AcZ, minVal, maxVal, -90, 90);

      x = RAD_TO_DEG * (atan2(-yAng, -zAng) + PI);
      y = RAD_TO_DEG * (atan2(-xAng, -zAng) + PI);
      z = RAD_TO_DEG * (atan2(-yAng, -xAng) + PI);

      if (y >= 180) {
        y = -(360 - y);
      }

      if (y >= 100) {
        y = y - 180;
      }
      else if (y <= -100) {
        y = y + 180;
      }

      if (x > 270) {
        x = (x - 360);
      }
      else if (x > 90) {
        x = -(x - 180);
      }

      xAngle = (xAngle + x);
    }
    xAngle = xAngle / 50;
  }

  Serial.print("AngleY= ");
  Serial.print(y);

  Serial.print("AngleX= ");
  Serial.println(xAngle);
  Serial.println("-----------------------------------------");

  // Ciclo dos motores: Ton ligado, Toff desligado.
  // Se a cinta inclina mais que ANGLE para um lado, o ramo desse lado não liga.
  if (power)
  {
    if ((millis() - oldTime < Ton))
    {
      if (xAngle >= ANGLE) {
        digitalWrite(LEFT, LOW);
        digitalWrite(CENTER, HIGH);
        digitalWrite(RIGHT, HIGH);
      } else if (xAngle <= -ANGLE) {
        digitalWrite(RIGHT, LOW);
        digitalWrite(LEFT, HIGH);
        digitalWrite(CENTER, HIGH);
      } else {
        digitalWrite(LEFT, HIGH);
        digitalWrite(CENTER, HIGH);
        digitalWrite(RIGHT, HIGH);
      }
    }
    else if ((millis() - oldTime < (Ton + Toff)))
    {
      digitalWrite(LEFT, LOW);
      digitalWrite(CENTER, LOW);
      digitalWrite(RIGHT, LOW);
    }
    else {
      digitalWrite(LEFT, LOW);
      digitalWrite(CENTER, LOW);
      digitalWrite(RIGHT, LOW);
      oldTime = millis();
    }
  }
  else
  {
    digitalWrite(LEFT, LOW);
    digitalWrite(CENTER, LOW);
    digitalWrite(RIGHT, LOW);
  }

#ifdef PIGSAVE_WIFI
  // Sem cliente conectado, volta direto ao início do loop
  if (WiFi.status() == WL_CONNECTED && !handleClient()) {
    return;
  }
#endif

  delay(1000);
}

#ifdef PIGSAVE_WIFI
// Servidor HTTP simples usado pelo aplicativo para ler e alterar a cinta
void sendEmptyResponse(WiFiClient &client, const __FlashStringHelper *type)
{
  client.println(F("HTTP/1.0 200 OK"));
  client.print(F("Content-Type: "));
  client.println(type);
  client.println(F("Connection: close"));
  client.println();
  client.stop();
}

bool handleClient()
{
  WiFiClient client = server.available(); // verifica se o aplicativo se conectou
  if (!client) return false;

  String currentLine = "";       // guarda a linha atual da requisição
  while (client.connected())
  { // enquanto o cliente estiver conectado
    if (client.available())
    { // se há bytes para ler do cliente
      char c = client.read();
      header += c;

      if (c == '\n')
      { // fim de uma linha
        // linha em branco indica o fim da requisição HTTP, então responde
        if (currentLine.length() == 0)
        {
          if (header.indexOf("GetDados") >= 0)
          {
            char buffer[16];
            const size_t capacity = JSON_OBJECT_SIZE(9);
            DynamicJsonDocument doc(capacity);
            itoa(Ton, buffer, 10);
            doc["Ton"] = buffer;
            itoa(Toff, buffer, 10);
            doc["Toff"] = buffer;
            doc["status"] = power;
            itoa(final_do_ip, buffer, 10);
            doc["final do ip"] = buffer;
            doc["nome"] = nome;
            doc["angle"] = yAngle;
            itoa(bateria, buffer, 10);
            doc["bateria"] = buffer;

            client.println(F("HTTP/1.0 200 OK"));
            client.println(F("Content-Type: json"));
            client.println(F("Connection: close"));
            client.print(F("Content-Length: "));
            client.println(measureJsonPretty(doc));
            client.println();

            serializeJsonPretty(doc, client);
            client.stop();
          }
          else if (header.indexOf("GET /ton") >= 0)
          {
            // GET /tonNNN define o tempo ligado em segundos
            Ton = header.substring(8, 11).toInt() * 1000;
            sendEmptyResponse(client, F("ton"));
          }
          else if (header.indexOf("GET /tof") >= 0)
          {
            // GET /tofNNN define o tempo desligado em segundos
            Toff = header.substring(8, 11).toInt() * 1000;
            sendEmptyResponse(client, F("tof"));
          }
          else if (header.indexOf("GET /OFF") >= 0)
          {
            power = false;
            sendEmptyResponse(client, F("OFF"));
          }
          else if (header.indexOf("GET /ON") >= 0)
          {
            power = true;
            oldTime = millis();
            sendEmptyResponse(client, F("ON"));
          }

          // a resposta HTTP termina com outra linha em branco
          client.println();
          break;
        }
        else
        { // nova linha, então limpa a linha atual
          currentLine = "";
        }
      }
      else if (c != '\r')
      { // qualquer caractere exceto o retorno de carro
        currentLine += c; // é adicionado à linha atual
      }
    }
  }
  header = "";
  client.stop();
  return true;
}
#endif
