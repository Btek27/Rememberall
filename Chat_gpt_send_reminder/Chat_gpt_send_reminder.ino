// Load Wi-Fi library
#include "WiFi.h"
#include <HTTPClient.h>
#include <UrlEncode.h>

// Replace with your network credentials
const char* ssid = "xxxxxxxxxxxxx";
const char* password = "xxxxxxxxxxxxx";

String phoneNumber = "+xxxxxxxxxxxxx";
String apiKey = "xxxxxxxxxxxxx";


const char* chatgpt_token = "xxxxxxxxxxxxx";

char chatgpt_server[] = "https://api.openai.com/v1/completions";



// Set web server port number to 80
WiFiServer server(80);
WiFiClient client1;

HTTPClient https;

String chatgpt_setup = "Write a short Reminder of the following information: ";
String chatGPT_prompt;
String chatGPT_response;
String json_String;
uint16_t dataStart = 0;
uint16_t dataEnd = 0;
String dataStr;
int httpCode = 0;

typedef enum {
  do_webserver_index,
  send_chatgpt_request,
  get_chatgpt_list,
} STATE_;

STATE_ currentState;

void WiFiConnect(void) {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  currentState = do_webserver_index;
}

const char html_page[] PROGMEM = {
  "HTTP/1.1 200 OK\r\n"
  "Content-Type: text/html\r\n"
  "Connection: close\r\n" 
  "\r\n"
  "<!DOCTYPE HTML>\r\n"
  "<html>\r\n"
  "<head>\r\n"
  "<meta charset=\"UTF-8\">\r\n"
  "<title>Rememberall: ChatGPT</title>\r\n"
  "</head>\r\n"
  "<body>\r\n"
  "<h1 align=\"center\">The Magical</h1>\r\n"
  "<h1 align=\"center\">Rememberall</h1>\r\n"
  "<div style=\"text-align:center;vertical-align:middle;\">"
  "<form action=\"/\" method=\"post\">"
  "<input type=\"text\" placeholder=\"Enter Your Reminder\" size=\"35\" name=\"chatgpttext\" required=\"required\"/>\r\n"
  "<input type=\"submit\" value=\"Send\" style=\"height:30px; width:80px;\"/>"
  "</form>"
  "</div>"
  "</p>\r\n"
  "</body>\r\n"
  "<html>\r\n"
};

void sendMessage(String message){

  // Data to send with HTTP POST
  String url = "https://api.callmebot.com/whatsapp.php?phone=" + phoneNumber + "&apikey=" + apiKey + "&text=" + urlEncode(message);    
  HTTPClient http;
  http.begin(url);

  // Specify content-type header
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  // Send HTTP POST request
  int httpResponseCode = http.POST(url);
  if (httpResponseCode == 200){
    Serial.print("Message sent successfully\n");
  }
  else{
    Serial.println("Error sending the message\n");
    Serial.print("HTTP response code: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

void setup() {
  Serial.begin(115200);

  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  while (!Serial)
    ;

  Serial.println("WiFi Setup done!");

  WiFiConnect();
  // Start the TCP server server
  server.begin();
  Serial.print("Server on");
}

void loop() {
  switch (currentState) {
    case do_webserver_index:
      client1 = server.available();
      if (client1) {
        boolean currentLineIsBlank = true;
        while (client1.connected()) {
          if (client1.available()) {
            char c = client1.read(); 
            json_String += c;
            if (c == '\n' && currentLineIsBlank) {
              dataStr = json_String.substring(0, 4);
              Serial.println(dataStr);
              if (dataStr == "GET ") {
                client1.print(html_page);
              } else if (dataStr == "POST") {
                json_String = "";
                while (client1.available()) {
                  json_String += (char)client1.read();
                }
                Serial.println(json_String);
                dataStart = json_String.indexOf("chatgpttext=") + strlen("chatgpttext=");  // parse the request for the following content
                chatGPT_prompt = json_String.substring(dataStart, json_String.length());
                client1.print(html_page);
                Serial.print("Your Reminder is: ");
                Serial.println(chatGPT_prompt);
                // close the connection:
                delay(10);
                client1.stop();
                currentState = send_chatgpt_request;
              }
              json_String = "";
              break;
            }
            if (c == '\n') {
              // you're starting a new line
              currentLineIsBlank = true;
            } else if (c != '\r') {
              // you've gotten a character on the current line
              currentLineIsBlank = false;
            }
          }
        }
      }
      delay(1000);
      break;
    case send_chatgpt_request:
      if (https.begin(chatgpt_server)) { 
        https.addHeader("Content-Type", "application/json");
        String token_key = String("Bearer ") + chatgpt_token;
        https.addHeader("Authorization", token_key);
        String payload = String("{\"model\": \"gpt-3.5-turbo-instruct\", \"prompt\": \"")+ chatgpt_setup + chatGPT_prompt + String("\", \"temperature\": 0.75, \"max_tokens\": 100}");  //Instead of TEXT as Payload, can be JSON as Paylaod
        httpCode = https.POST(payload);                                                                                                                          // start connection and send HTTP header
        payload = "";
        currentState = get_chatgpt_list;
      } else {
        Serial.println("[HTTPS] Unable to connect");
        delay(1000);
      }
      break;
    case get_chatgpt_list:
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = https.getString();
        dataStart = payload.indexOf("\\n\\n") + strlen("\\n\\n");
        dataEnd = payload.indexOf("\",", dataStart);
        chatGPT_response = payload.substring(dataStart, dataEnd);
        Serial.println(chatGPT_response);
        currentState = do_webserver_index;
      } else {
        Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        while (1)
          ;
      }
      https.end();
      sendMessage(chatGPT_response);
      delay(10000);
      break;
  }
}
