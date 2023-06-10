#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "classic_setup.h"
#include "ESPAsyncWebServer.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

/* Pour pouvoir envoyer les données à Mongo Atlas */
//#define mongo "https://eu-west-2.aws.data.mongodb-api.com/app/data-fuvth/endpoint/data/v1"
#include <WiFiClientSecure.h>

/* Application Node.JS */
const char* service = "https://servicejs-guo-hakobyan.onrender.com/insert";
//const char* service = "http://172.20.10.2/insert";

/* Capteur de temperature OneWire */
#include "OneWire.h"
#include "DallasTemperature.h"
OneWire oneWire(23);
DallasTemperature tempSensor(&oneWire);
/* Capteur de temperature DHT */
//#include <DHT.h>
//#define DHTPIN 4
//#define DHTTYPE DHT11
//DHT dht(DHTPIN, DHTTYPE);

/*===== Create AsyncWebServer object on port 80 ========*/
AsyncWebServer server(80);

/*===== MQTT broker/server ========*/
const char* mqtt_server = "test.mosquitto.org";

/*===== MQTT TOPIC ===============*/
#define TOPIC_PISCINE "uca/iot/piscine"

/*===== ESP is MQTT Client =======*/
WiFiClient espClient;
PubSubClient client(espClient);

/*============= GPIO ==============*/
const int LEDRed = 19;
const int LEDGreen = 21;
const int LightPin = A5;
const int ledPinOnBoard = 2;
float temperature = 0;
int light = 0;

unsigned long ouverture;
const unsigned long dureeOuverture = 30000; // 30 sec

// Résidence Newton, 2400 Route des Dolines, 06560 Valbonne, France
double latitude = 43.62454;
double longitude = 7.050628;
// 189 Impasse des PLATANES, 83600 Fréjus, France
//double latitude = 43.440419;
//double longitude = 6.761018;

const char* id_piscine = "P_22107039";

String WifiSSID;
String MAC;
String IP;

int isTheHotter;

// Ce tableau contiendra tous les users présents dans l'enceinte de la piscine
String inThePool[10]; // Supposons que la piscine peut accueillir 10 personnes max.

String target_ip = "127.0.0.1";
int target_port = 1880;
int sp = 2;

int cpt;

/*===== Arduino IDE paradigm : setup+loop =====*/
void setup() {
  Serial.begin(9600);
  while (!Serial);

  connect_wifi();
  print_network_status();

  cpt = 0;

  // Initialize the output variables as outputs
  pinMode(LEDRed, OUTPUT);
  pinMode(LEDGreen, OUTPUT);
  digitalWrite(LEDRed, LOW);
  digitalWrite(LEDGreen, LOW);
  pinMode(ledPinOnBoard, OUTPUT);

  // Init temperature sensor
  tempSensor.begin();

  /* SSID/MAC/IP */
  WifiSSID = String(WiFi.SSID());
  MAC = String(WiFi.macAddress());
  IP = WiFi.localIP().toString();

  isTheHotter = 0;

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Hello from ESP32!");
  });

  /* Handler sur /open */
  server.on("/open", HTTP_GET, [](AsyncWebServerRequest *request){
    // On récupère le client id
    String cID = String(request->arg("idu"));
    String cLat = String(request->arg("lat"));
    String cLon = String(request->arg("lon"));
    String cDate = String(request->arg("date"));
    //Serial.println("id: "+String(cID)+" | date: "+String(cDate)+" | lat: "+String(cLat)+" | lon: "+String(cLon));

    // Si la distance est inférieur à 100 m
    int distance = get_Distance(cLat, cLon);
    if(distance <= 100) {
      // S'il n'est pas déjà enregistré, on l'ajoute. Signifiant que l'utilisateur veut entrer
      if(is_In_Array(cID, inThePool, 10)==false) {
        // On trouve le prochain index libre
        int idx = find_Next_Index(inThePool, 10);
        if(idx != -1) {
          inThePool[idx] = cID; // On ajoute ce client id au tableau
          request->send(200, "text/plain", "Pool doors openned!"); // Ouverture
            
          // Insertion dans mongodb
          //ouverture = millis();
          Serial.println("Ouverture de la porte!");
          set_LED("blue", 1);
          send_to_service(String(id_piscine), String(cID), String(cDate), "let me in!");
          //send_to_service(String(id_piscine), String(cID), "09/06/2023", "let me in!");
          
        } else {
          // Si le tableau est plein, alors la piscine accueil le nombre max de personnes
          request->send(200, "text/plain", "Access denied, the pool is full of people!"); // Refus
        }
      } else {
        // S'il est déjà enregistré, on le retire. Signifiant que l'utilisateur veut sortir
        remove_Item_From_Array(cID, inThePool, 10);
  
        // Insertion dans mongodb
        //ouverture = millis();
        Serial.println("Ouverture de la porte!");
        set_LED("blue", 1);
        send_to_service(String(id_piscine), String(cID), String(cDate), "let me out!");
        //send_to_service(String(id_piscine), String(cID), "09/06/2023", "let me out!");
        
        request->send(200, "text/plain", "Pool doors openned! See you soon!"); // Ouverture
      }
      request->send(200, "text/plain", "Access denied, you are too far from the pool"); // Refus
    }
  });

  server.begin();

  // set server of our client
  client.setServer(mqtt_server, 1883);
  // set callback when publishes arrive for the subscribed topic
  client.setCallback(mqtt_pubcallback);
}

/*============= FUNCTIONS ===================*/
// Trouve le prochain index libre dans un array
int find_Next_Index(String array[], int size) {
  for (int i = 0; i < size; i++) {
    if (array[i].length() == 0) {
      return i;
    }
  }
  return -1;  // Le tableau est plein!
}

// Trouve si un item est dans un array
bool is_In_Array(const String& item, String array[], int size) {
  for (int i = 0; i < size; i++) {
    if (array[i] == item) {
      return true;  // Trouvé!    
    }
  }
  return false;  // Non trouvé!
}

void remove_Item_From_Array(const String& item, String array[], int size) {
    for (int i = 0; i < size; i++) {
        if (array[i] == item) {
            for (int j = i; j < size - 1; j++) {
                array[j] = array[j + 1];
            }
            array[size - 1] = "";
            break;
        }
    }
}

void set_LED(String n, int v) {
  if (n == "green") {
    digitalWrite(LEDGreen, v);
  }
  else if(n == "red") {
    digitalWrite(LEDRed, v);
  } else {
    digitalWrite(ledPinOnBoard, v);
  }
}

// Version OneWire
float get_Temperature() {
  float t;
  tempSensor.requestTemperaturesByIndex(0);
  t = tempSensor.getTempCByIndex(0);
  return t;
}

// Version DHT
/*float get_Temperature() {
  float t;
  float h;
  t = dht.readTemperature();
  h = dht.readHumidity();
  if (isnan(t)) {
    return 0;
  }
  return t;
}*/

int get_Light(int LightPin) {
  return analogRead(LightPin);
}

double get_Distance(String lat, String lon) {
  double distance;
  
  WiFiClient client;
  HTTPClient http;
  String distanceServer = "http://api.distancematrix.ai/maps/api/distancematrix/json?origins=" + String(latitude, 5) + "," + String(longitude, 5) + "&destinations=" + lat + "," + lon + "&departure_time=now&key=MpbudmN4pzUikeEcoNt08t2MaE8bm";  
  http.begin(client, distanceServer);
  int httpResponseCode = http.GET();
  String data = http.getString();
  StaticJsonDocument<512> jdoc;
  deserializeJson(jdoc, data);
  distance = jdoc["rows"][0]["elements"][0]["distance"]["value"];
  http.end();

  return distance;
}

void send_to_service(String pool, String cli, String date, String request) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, String(service));
  
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = "{\"pool\":\""+pool+"\",\"client\":\""+cli+"\",\"date\":\""+date+"\",\"request\":\""+request+"\"}"; 
  
  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    String response = http.getString();
    Serial.println(response);
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

/*============== PUBLISH ===================*/
void mqtt_publish_data() {
  static uint32_t mqttTick = 0;
  if (sp <= 0 || ((millis() - mqttTick) < sp * 1000)) {
    return;
  } else {
    mqttTick = millis();

    char payload[512];
    client.setBufferSize(512); // Sinon le message n'est pas envoyé car trop long
    StaticJsonDocument<512> jsondoc;
  
    jsondoc["status"]["temperature"] = temperature;
    jsondoc["status"]["light"] = light;
    jsondoc["status"]["heat"] = String(digitalRead(LEDRed));
    jsondoc["status"]["cold"] = String(digitalRead(LEDGreen));
    if (get_Light(LightPin) <= 100) {
      jsondoc["status"]["fire"] = 1;
    }
    else {
      jsondoc["status"]["fire"] = 0;
    }
    jsondoc["info"]["ident"] = id_piscine;
    jsondoc["info"]["user"] = "Arman Hakobyan";
    jsondoc["info"]["description"] = "A Valbonne";
    //jsondoc["info"]["uptime"] = String(millis() / 1000);
    //jsondoc["info"]["ssid"] = WifiSSID;
    jsondoc["info"]["mac"] = MAC;
    jsondoc["info"]["ip"] = IP;
    jsondoc["info"]["loc"]["lat"] = latitude;
    jsondoc["info"]["loc"]["lon"] = longitude;
    //jsondoc["regul"]["sh"] = 30;
    //jsondoc["regul"]["sb"] = 10;
    jsondoc["reporthost"]["target_ip"] = target_ip;
    jsondoc["reporthost"]["target_port"] = String(target_port);
    jsondoc["reporthost"]["sp"] = String(sp);
    if (digitalRead(LEDGreen) == 1 || isTheHotter == 0) {
      jsondoc["piscine"]["led"] = "Green";
      jsondoc["piscine"]["color"] = "Green";
      jsondoc["piscine"]["fillcolor"] = "Green";
    }
    if (digitalRead(LEDRed) == 1 || isTheHotter == 1) {
      jsondoc["piscine"]["led"] = "Red";
      jsondoc["piscine"]["color"] = "Red";
      jsondoc["piscine"]["fillcolor"] = "Red";
   }
    if (get_Light(LightPin) < 2500) {
      jsondoc["piscine"]["presence"] = "Oui";
    }
    else {
      jsondoc["piscine"]["presence"] = "Non";
    }

  
    serializeJson(jsondoc, payload);
  
    // Publish
    if (!client.publish(TOPIC_PISCINE, payload)) {
      Serial.println("Error publishing message");
      Serial.println(strlen(payload));
    }
  }
}

/*============== CALLBACK ===================*/
void mqtt_pubcallback(char* topic, byte* message, unsigned int length) {
  /* Callback if a message is published on this topic. */
  String messageTemp;
  for (int i = 0; i < length; i++) {
    messageTemp += (char)message[i];
  }

  if (String(topic) == TOPIC_PISCINE) {
    StaticJsonDocument<512> jsondoc;
    deserializeJson(jsondoc, messageTemp);
    float topicTemp = float(jsondoc["status"]["temperature"]);
    String topicID = jsondoc["info"]["ident"];
    String topicMAC = jsondoc["info"]["mac"];
    String topicLAT = jsondoc["info"]["loc"]["lat"];
    String topicLON = jsondoc["info"]["loc"]["lon"];
    
    //Serial.println(topicID+" is at "+topicLAT+", "+topicLON);
    
    if(topicID != id_piscine){ // Si il ne s'agit pas de mon adresse mac, donc pas mon propre publish
      /*Serial.print("Message arrived on topic : ");
      Serial.println(topic);
      Serial.print("=> ");*/
    
      // Byte list to String and print to Serial
      messageTemp = "";
      for (int i = 0; i < length; i++) {
        Serial.print((char)message[i]);
        messageTemp += (char)message[i];
      }
      Serial.println();

      double topicDistance = get_Distance(topicLAT, topicLON);
      Serial.println("Distance entre Moi est "+topicID+": "+String(topicDistance/1000)+" km");
      //if(topicDistance != 9999999.00) {
      if(topicDistance/1000 <= 25){
        if(temperature > topicTemp){
          Serial.println("Je suis la piscine la plus chaude dans un rayon de 25km");
          isTheHotter = 1;
          set_LED("green", 0);
          set_LED("red", 1);
        }
        else {
          Serial.println("Rien à signaler niveau temperature");
          isTheHotter = 0;
          set_LED("green", 1);
          set_LED("red", 0);
        }
      }
      //}
    }
  }
}

/*============= SUBSCRIBE =====================*/
void mqtt_mysubscribe(char *topic) {
  /* Subscribe to a MQTT topic  */
  while (!client.connected()) { // Loop until we're reconnected

    Serial.print("Attempting MQTT connection...");
    if (client.connect("armanhkb", NULL, NULL)) { // ID & Password
      Serial.println("connected");
      // then Subscribe topic
      client.subscribe(topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());

      Serial.println(" try again in 5 seconds");
      delay(5000); // Wait 5 seconds before retrying
    }
  }
}

/*================= LOOP ======================*/
void loop() {
  int32_t period = 10000; // 10 sec  
  /*--- subscribe to TOPIC_PISCINE if not yet ! */
  if (!client.connected()) {
    mqtt_mysubscribe((char *)(TOPIC_PISCINE));
  }

  // Ferme la porte si 30 sec sont passées depuis son ouverture
  /*if (millis() - ouverture >= dureeOuverture) {
    set_LED("blue", 0);
    
    // Reset
    ouverture = 0;
  }*/
  
  /*--- Publish Temperature periodically   */
  delay(period);

  if(digitalRead(ledPinOnBoard)) {
    cpt += 1;
    if(cpt>=3) {
      set_LED("blue", 0);
      Serial.println("Fermeture de la porte!");
      cpt = 0; 
    }
  }
  
  temperature = get_Temperature();
  light = get_Light(LightPin);

  // Debug
  //Serial.println("Temperature : "+String(temperature)+" |Light : "+String(light));

  // MQTT Publish
  mqtt_publish_data();

  /* Process MQTT ... une fois par loop() ! */
  client.loop();
}
