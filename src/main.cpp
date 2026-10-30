#include <Arduino.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>

WiFiServer server(9080);
WiFiManager wifiManager;
WiFiClientSecure client;

const char *serverUrl = "http://192.168.15.4:3333/ewent";

// Estrutura para sensores
struct Sensor
{
  uint8_t pin;
  uint8_t ledPin;
  const char *id;
  const char *descricao;
  bool state = false;
  bool triggered = false;

  Sensor(uint8_t p, uint8_t led, const char *i, const char *d)
      : pin(p), ledPin(led), id(i), descricao(d), state(false), triggered(false) {}
};

constexpr uint8_t BT_RESET_PIN = 13;
constexpr uint8_t LED_ALARM_PIN = 18;
constexpr uint8_t LED_RESET_REMOTO = 2;

Sensor sensores[] = {
    {32, 23, "1", "Sensor IR lado FRENTE"},
    {33, 22, "2", "Sensor IR lado DIREITO"},
    {25, 21, "3", "Sensor IR lado ESQUERDO"},
    {26, 19, "4", "Sensor IR lado TRÁS"},
    {27, 15, "5", "Sensor TESTE"}};

bool memoryAlarm = false;
String header;
unsigned long currentTime, previousTime;
const unsigned long timeoutTime = 3000;

void resetEvents()
{
  for (auto &s : sensores)
  {
    if (!s.state)
    {
      s.triggered = false;
      digitalWrite(s.ledPin, LOW);
    }
  }
  memoryAlarm = false;
  digitalWrite(LED_ALARM_PIN, LOW);
}

void sendEventToServer(const char *sensor_id, const char *status, const char *description)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi desconectado!");
    return;
  }

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = String("{\"sensor_id\":\"") + sensor_id +
                       "\",\"user_id\":\"1\",\"status\":\"" + status +
                       "\",\"description\":\"" + description + "\"}";

  int code = http.POST(jsonPayload);
  if (code > 0)
  {
    Serial.printf("[HTTP] POST... Código: %d\n", code);
  }
  else
  {
    Serial.printf("[HTTP] POST... Falha: %s\n", http.errorToString(code).c_str());
  }
  http.end();
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Configurar Rede Wifi");

  wifiManager.autoConnect("sidebit", "sidebit#8");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.printf("Conectado! IP: %s\n", WiFi.localIP().toString().c_str());

  if (!MDNS.begin("sidebit"))
  {
    Serial.println("Erro ao iniciar MDNS!");
    while (1)
      delay(1000);
  }
  MDNS.addService("http", "tcp", 9080);

  server.begin();

  pinMode(LED_RESET_REMOTO, OUTPUT);
  pinMode(LED_ALARM_PIN, OUTPUT);
  pinMode(BT_RESET_PIN, INPUT_PULLUP);

  for (auto &s : sensores)
  {
    pinMode(s.pin, INPUT_PULLUP);
    pinMode(s.ledPin, OUTPUT);
    digitalWrite(s.ledPin, LOW);
  }
  digitalWrite(LED_ALARM_PIN, LOW);

  Serial.println("Sistema iniciado!");
}

void handleSensor(Sensor &s)
{
  bool newState = digitalRead(s.pin);
  if (newState == s.state)
    return; // sem mudança

  s.state = newState;
  if (s.state)
  {
    s.triggered = true;
    memoryAlarm = true;
    digitalWrite(s.ledPin, HIGH);
    digitalWrite(LED_ALARM_PIN, HIGH);
    sendEventToServer(s.id, "ALARME", s.descricao);
  }
  else
  {
      s.triggered = false;
  //    digitalWrite(s.ledPin, LOW);
      sendEventToServer(s.id, "NORMAL", s.descricao);
  }
}

void handleWebClient(WiFiClient &client)
{
  currentTime = millis();
  previousTime = currentTime;
  String currentLine = "";

  while (client.connected() && (millis() - previousTime <= timeoutTime))
  {
    if (client.available())
    {
      char c = client.read();
      header += c;
      if (c == '\n' && currentLine.length() == 0)
      {
        client.println("HTTP/1.1 200 OK\r\nContent-type:text/html\r\nConnection: close\r\n");

        if (header.indexOf("GET /R") >= 0)
        {
          digitalWrite(LED_RESET_REMOTO, HIGH);
          resetEvents();
          delay(100);
          digitalWrite(LED_RESET_REMOTO, LOW);
        }
        if (header.indexOf("GET /B") >= 0)
        {
          ESP.restart();
        }

        // Página HTML
        client.println("<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><title>SIDEBIT</title>");
        client.println("<style>.sensorON{background:#0f0}.sensorOFF{background:#f00;color:#fff}</style></head><body>");
        client.println("<h1>Sidebit - Monitoramento</h1><div>");
        for (auto &s : sensores)
        {
          client.printf("<div class='sensor %s'>%s</div>", s.state ? "sensorOFF" : "sensorON", s.descricao);
        }
        client.println("<a href='/R'><button>RESETAR</button></a><a href='/B'><button>REBOOT</button></a>");
        client.println("</div><p>&copy;2025 - Sidebit</p></body></html>");
        break;
      }
      else if (c == '\n')
      {
        currentLine = "";
      }
      else if (c != '\r')
      {
        currentLine += c;
      }
    }
  }

  header = "";
  client.stop();
}

void loop()
{
  for (auto &s : sensores)
  {
    handleSensor(s);
  }

  if (memoryAlarm)
  {
    digitalWrite(LED_ALARM_PIN, HIGH);
  }

  if (digitalRead(BT_RESET_PIN) == LOW)
  {
    resetEvents();
    delay(50); // debounce
  }

  WiFiClient client = server.accept();
  if (client)
  {
    handleWebClient(client);
  }

  delay(100); // reduz uso da CPU
}
