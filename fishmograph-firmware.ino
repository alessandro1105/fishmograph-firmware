#include <FishinoWebServer.h>
#include <SD.h>
#include <D7S.h>
#include <FishinoRTC.h>
#include <ArduinoJson.h>

//---------------- CONSTANTS ----------------
//NETWORKS
//put here your network settings
#define MY_SSID "RavanelloWifi" //ssid
#define MY_PASS "ravanello987654321" //password

//IP ADDRESS CONSTANTS
#define IPADDR  192, 168,   1, 200 //ip of the server
#define GATEWAY 192, 168,   1, 254 //gateway of the network
#define NETMASK 255, 255, 255, 0 //netmask of the network

//SD CARD
#ifdef SDCS
  #define SD_CS SDCS
#else
  #define SD_CS 4
#endif

//D7S INTERRUPT PINS
#define INT1_PIN 2 //interrupt pin INT1 of D7S attached to pin 2 of Arduino
#define INT2_PIN 3 //interrupt pin INT2 of D7S attached to pin 3 of Arduino

//TIME FOR WHICH THE NOTIFICATION IS VALID
#define NOTIFICATION_VALIDITY_TIME 120000 //time in ms (2 minutes)

typedef enum status {
   NONE = 0,
   EARTHQUAKE_STARTED = 1,
   EARTHQUAKE_ENDED = 2,
   SHUTOFF_OCCURS = 3,
   COLLAPSE_OCCURS = 4
};

//---------------- VARIABLES ----------------
//Network
IPAddress ip(IPADDR);
IPAddress gateway(GATEWAY);
IPAddress netmask(NETMASK);

//SD card
Sd2Card card;
SdVolume volume;
SdFile root;
SdFile file;

//web session variables
long sessionID = -1; //dentificator of the web session (only positive number are valid session ID)
bool isUserLogged = false; //if true the user is logged in this session

//The web server object
FishinoWebServer web(80);

//earthquake handling variables
//earthquake data current/last earthquake
struct earthquake_t {
  long timestamp; //timestamp at which the eathquake started
  uint8_t events; //first bit earyhquake occuring, second bit earthquake stopped, third bit shutoff, fourth bit collapse
  long shutoffTimestamp; //shutoff timestamp
  long collapseTimestamp; //shutoff timestamp
  uint8_t notifications; //first bit eathquake started, second bit earthquake ended, third bit shutoff, fouth bit collapse
  long endingTime; //millis at which the earthquake ended
} earthquake;

//---------------- D7S EVENTS HANDLERS ----------------
//handler for the START_EARTHQUAKE D7S event
void startEarthquakeHandler() {  
  earthquake.timestamp = RTC.now().getUnixTime(); //save the timestamp
  //notfications
  earthquake.events = 0x01; //earthquake started
  earthquake.notifications = 0x00; //reset notifications
  //reset the events shutoff/collapse memorized into the D7S
  D7S.resetEvents();

  Serial.println(F("--- EARTHQUAKE STARTED ---"));
}

//handler for the END_EARTHQUAKE D7S event
void endEarthquakeHandler(float si, float pga, float temperature) {
  //saving millis at which the earthquake ended (used in the notifications)
  earthquake.endingTime = millis();
  earthquake.events |= 0x02; //earthquake ended
  earthquake.events &= 0x0E; //earthquake is not occuring because it's ended
  //disable interrupt to prevent corrupting the file
  noInterrupts();
  //position to last data
  unsigned long position = 0;
  //open file
  if (!file.open(&root, "data.txt", O_READ | O_WRITE)) {
    file.open(&root, "data.txt", O_CREAT | O_WRITE | O_TRUNC);
  }

  Serial.println("0");

  //counting the data
  char c;
  while ((c = file.read()) != -1) {
    if (c == '}') {
      position = file.curPosition();
    }
  }

  Serial.println("1");
  
  if (position == 0) { //if is the first data
    file.rewind();
    file.print(F("[{\"tmsp\": "));
  } else { //there are other data
    //reset file to the last postion
    file.seekSet(position);
    file.print(F(", {\"tmsp\": "));
  }

  Serial.println("2");

  file.print(earthquake.timestamp);
  file.print(F(", \"si\": "));
  file.print(si);
  file.print(F(", \"pga\": "));
  file.print(pga);
  file.print(F(", \"temp\": "));
  file.print(temperature);
  file.print(F(", \"shut\": "));
  if ((earthquake.events & 0x04) >> 2) {
    file.print(earthquake.shutoffTimestamp);
  } else {
    file.print(0);
  }
  file.print(F(", \"coll\": "));
  if ((earthquake.events & 0x08) >> 3) {
    file.print(earthquake.collapseTimestamp);
  } else {
    file.print(0);
  }
  file.print(F("}]"));

  Serial.println("3");

  //chiudo il file
  file.close();

  Serial.println("4");

  //enable interrupts
  interrupts();

  Serial.println(F("--- EARTHQUAKE ENDED ---"));
}

//handler for the SHUTOFF D7S event
void shutoffHandler() {
  Serial.println(F("--- SHUTOFF ---"));
  //saving the shutoff event
  earthquake.events |= 0x04;
  earthquake.shutoffTimestamp = RTC.now().getUnixTime();
} 

//handler for the COLLAPSE D7S event
void collapseHandler() {
  Serial.println(F("--- COLLAPSE ---"));
  //saving the collapse event
  earthquake.events |= 0x08;
  earthquake.collapseTimestamp = RTC.now().getUnixTime();
}

//---------------- SESSION AND USER HANDLERS ----------------
//start a web session
void startSession(FishinoWebServer &web) {
  long cookieSessionValue = -1;
  //read the session cookie
  const char *cookieSession = web.getHeaderValue("Cookie");
  //if the request has send a cookie
  if (cookieSession != 0) {
    cookieSessionValue = atoi(cookieSession +8);
  }  
  //check if the value of the cookie is different from the session ID we are currently handling
  if (cookieSessionValue != sessionID || cookieSessionValue < 0) {
    //we need to generate a new sessionID
    sessionID = RTC.now().getUnixTime(); //using timestamp for uniqness
    //reset isUserLogged session variable
    isUserLogged = false;
  }
}

// send the web session cookie
void sendSessionCookie(FishinoWebServer &web) {
  //we need to send the "set-cookie" header with the cookie containing the session ID
  char cookie[30];
  sprintf(cookie, "session=%d", sessionID);
  sendHTTPHeader(web, F("Set-Cookie"), cookie);
}

//get the user password from a file
char *getUserPassword() {
  return "fishmograph123";
}

//---------------- WEB SERVER HANDLERS ----------------
//handler for the url GET "/"
bool indexHandler(FishinoWebServer &web) {
  sendFile(web, "index.htm");
  return true;
}

//handler for the url POST "/login"
bool loginHandler(FishinoWebServer &web) {
  Serial.println("loginHandler");
  //start the session handling
  startSession(web);
  //if the user is already autenticated we do not parse the body of the request
  if (isUserLogged) {
    sendHTTPStatusCode(web, 204);
  
  // the use is not autenticated and we need to handle the body of the request
  } else {
    FishinoClient client = web.getClient();
    //getting the body of the request
    char body[client.available() +1]; //we create a buffer of the correct size of the body +1 (the null terminator)
    int i = 0;
    while (client.available() && i < 300) {
      body[i++] = (char) client.read();
    }
    body[i] = 0;

    //parsing the request
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(body);

    //the body of the request is incorrect
    if (!root.success() || !root.containsKey("password")) {
      sendHTTPStatusCode(web, 400);

    } else {
      //getting the password from the body
      const char *password = root["password"];

      //if the password is correct
      if (strcmp(password, getUserPassword()) == 0) {
        //the login is successful
        isUserLogged = true;
        //send status code
        sendHTTPStatusCode(web, 204);

      // the password is incorrect
      } else {
        sendHTTPStatusCode(web, 401);
      }

    }
  }

  //send the session "set-cookie" header
  sendSessionCookie(web);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  web.endHeaders();

  return true;
}

//handler for the url POST "/logout"
bool logoutHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);
  //if the user is logged
  if (isUserLogged) {
    isUserLogged = false;
    sendHTTPStatusCode(web, 204);
  
  //if the user is not logged
  } else {
    sendHTTPStatusCode(web, 401);
  }

  //send the session "set-cookie" header
  sendSessionCookie(web);
  web.endHeaders();

  return true;
}

//handler for the url GET "/status"
bool statusHandler(FishinoWebServer &web) {
  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  web.endHeaders();
  //getting the client
  FishinoClient& client = web.getClient();

  //response in JSON
  client.print(F("{\"status\": "));

  //earthquake occuring and the notifications has not been sent before
  if(earthquake.events & 0x01 && !(earthquake.notifications & 0x01)) {
    earthquake.notifications |= 0x01; //notifications sent
    //response
    client.print(EARTHQUAKE_STARTED);
    client.print(F(", \"tmsp\": "));
    client.print(earthquake.timestamp);
  
  //earthquake occuring and the shutoff notifications has not been sent before
  } else if (earthquake.events & 0x01 && earthquake.events & 0x04 && !(earthquake.notifications & 0x04)) { //SHTOFF
    earthquake.notifications |= 0x04; //notifications sent
    //response
    client.print(SHUTOFF_OCCURS);
    client.print(F(", \"tmsp\": "));
    client.print(earthquake.shutoffTimestamp);

  //earthquake occuring and the collapse notifications has not been sent before
  } else if (earthquake.events & 0x01 && earthquake.events & 0x08 && !(earthquake.notifications & 0x08)) { //COLLAPSE
    earthquake.notifications |= 0x08; //notifications sent
    //response
    client.print(COLLAPSE_OCCURS);
    client.print(F(", \"tmsp\": "));
    client.print(earthquake.collapseTimestamp);

  //earthquake endend and the notifications has not been sent before and there is still time to sent it
  } else if (earthquake.events & 0x02 && (millis() - earthquake.endingTime) <= NOTIFICATION_VALIDITY_TIME && !(earthquake.notifications & 0x02)) {
    earthquake.notifications |= 0x02; //notifications sent
    client.print(EARTHQUAKE_ENDED);  
  //no earthquakes
  } else {
    client.print(NONE);
  }
  
  //close the response
  client.println(F("}\n"));

  return true;
}

//handler for the url POST "/settings/initialize"
bool settingsInitializeHandler(FishinoWebServer &web) {
  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  web.endHeaders();
  //getting the client
  FishinoClient& client = web.getClient();

  //if there are no earthquakes occuring
  if (earthquake.events & 0x01) {
    Serial.println(F("Earthquake occuring!"));
    client.print(F("{\"status\": 2}\n"));
    return true;
  }

  //disable D7S interrupt handling
  D7S.stopInterruptHandling();

  Serial.print(F("Initializing..."));
  //start the initial installation procedure
  D7S.initialize();
  //wait until the D7S is ready (the initializing process is ended)
  while (!D7S.isReady()) {
    Serial.print(F("."));
    delay(500);
  }
  Serial.println(F("OK"));

  client.print(F("{\"status\": 0}\n"));

  //enabling D7S interrupt handling
  D7S.startInterruptHandling();

  return true;
}

//handler for the url POST "/settings/selftest"
bool settingsSelftestHandler(FishinoWebServer &web) {
  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  web.endHeaders();
  //getting the client
  FishinoClient& client = web.getClient();

  //if there are no earthquakes occuring
  if (earthquake.events & 0x01) {
    Serial.println(F("Earthquake occuring!"));
    client.print(F("{\"status\": 2}\n"));
    return true;
  }

  //disable D7S interrupt handling
  D7S.stopInterruptHandling();

  Serial.print(F("Selftest..."));
  D7S.selftest();
  //wait until the D7S selftest is ended
  while (!D7S.isReady()) {
    Serial.print(F("."));
    delay(500);
  }

  //response in JSON
  client.print(F("{\"status\": "));

  //checking the result
  if (D7S.getSelftestResult() == D7S_OK) {
    //send it's all ok
    client.print(0);
    Serial.println(F("OK"));
  } else {
    //send there is an error
    client.print(1);
    Serial.println(F("ERROR"));
  }
  //closing the message to the client
  client.println(F("}"));

  //enabling D7S interrupt handling
  D7S.startInterruptHandling();

  return true;
}

//handler for the url POST "/settings/clear/d7s"
bool settingsClearD7SHandler(FishinoWebServer &web) {
  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  web.endHeaders();
  //getting the client
  FishinoClient& client = web.getClient();

  //clearing the memory of D7S
  D7S.clearEarthquakeData();

  client.print(F("{\"status\": 0}\n"));

  return true;
}

//handler for the url POST "/settings/clear/data"
bool settingsClearDataHandler(FishinoWebServer &web) {
  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  web.endHeaders();
  //getting the client
  FishinoClient& client = web.getClient();

  //creating a new file data.txt
  file.open(&root, "data.txt", O_CREAT | O_WRITE | O_TRUNC);
  file.print(F("[]"));
  file.close();

  client.print(F("{\"status\": 0}\n"));

  return true;
}

//handler for the url GET "/data/earthquakes"
bool dataHandler(FishinoWebServer &web) {
  //disable interrupt to prevent corrupting the file
  noInterrupts();
  //sending the file
  sendFile(web, "data.txt");
  //enable interrupts
  interrupts();
  return true;
}

//handler for the url GET "/data/d7s/lastest"
bool dataD7SHandler(FishinoWebServer &web) {
  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  web.endHeaders();
  //getting the client
  FishinoClient& client = web.getClient();

  client.print(F("[["));
  for (int i = 0; i < 5; i++) {
    client.print(F("{\"si\": "));
    client.print(D7S.getRankedSI(i));
    client.print(F(", \"pga\": "));
    client.print(D7S.getRankedPGA(i));
    client.print(F(", \"temp\": "));
    client.print(D7S.getRankedTemperature(i));
    client.print(F("}"));
    if (i != 4) {
      client.print(F(", "));
    }
  }
  client.print(F("],["));
  for (int i = 0; i < 5; i++) {
    client.print(F("{\"si\": "));
    client.print(D7S.getLastestSI(i));
    client.print(F(", \"pga\": "));
    client.print(D7S.getLastestPGA(i));
    client.print(F(", \"temp\": "));
    client.print(D7S.getLastestTemperature(i));
    client.print(F("}"));
    if (i != 4) {
      client.print(F(", "));
    }
  }
  client.print(F("]]\n"));

  return true;
  
}

//handler for the url GET "/" "*"
bool fileHandler(FishinoWebServer &web) {
  String filename = web.getFileFromPath(web.getPath());
  sendFile(web, filename.c_str());
  return true;
}

//---------------- WEBSERVER UTILS FUNCTIONS ----------------
// sends a file to client
void sendFile(FishinoWebServer& web, const char* filename) {
  if (!filename) {
    sendHTTPStatusCode(web, 404);
    web.endHeaders();
    web << F("Could not parse URL");
  } else {
    sendHTTPStatusCode(web, 200);

    Serial.print(F("Serving: "));
    Serial.println(filename);

    //svg type
    if (strstr(filename, ".SVG") != NULL) {
      web.sendContentType(F("image/svg+xml"));
      web << F("Cache-Control:public, max-age=900\r\n");

    //other types
    } else {
      FishinoWebServer::MimeType mimeType = web.getMimeTypeFromFilename(filename);
      web.sendContentType(mimeType);

      if(mimeType == FishinoWebServer::MIMETYPE_GIF ||
         mimeType == FishinoWebServer::MIMETYPE_JPG ||
         mimeType == FishinoWebServer::MIMETYPE_PNG ||
         mimeType == FishinoWebServer::MIMETYPE_ICO) {
        web << F("Cache-Control:public, max-age=900\r\n");
      }
    }
    web.endHeaders();
    if (file.open(&root, filename, O_READ)) {
      web.sendFile(file);
      file.close();
    } else {
      web << F("Could not find file: ") << filename << "\n";
    }
  }
}

//auxiliary function to send HTTP headers
void sendHTTPHeader(FishinoWebServer& web, const __FlashStringHelper *header, const __FlashStringHelper *value) {
  FishinoClient client = web.getClient();
  client.print(header);
  client.print(": ");
  client.println(value);
}

//auxiliary function to send HTTP headers
void sendHTTPHeader(FishinoWebServer& web, const __FlashStringHelper *header, const char * value) {
  FishinoClient client = web.getClient();
  client.print(header);
  client.print(": ");
  client.println(value);
}

//send HTTP status code
//FishinoWebServer close the header section of the response if the status code is != 200
void sendHTTPStatusCode(FishinoWebServer& web, uint16_t statusCode) {
  FishinoClient client = web.getClient();
  client << F("HTTP/1.1 ") << statusCode << F(" OK\r\n");
}


//---------------- INITIALIZE FUNCTIONS ----------------
//function to initialize the Fishino board
void initFishino() {
  //resetting fishino
  Serial.print(F("Resetting Fishino..."));
  while(!Fishino.reset())
  {
    Serial.print(F("."));
    delay(500);
  }
  Serial.println(F("OK"));
  //Connetiing Fishino to the WiFi
  Fishino.setMode(STATION_MODE);
  Serial.print(F("Connecting AP..."));
  while(!Fishino.begin(F(MY_SSID), F(MY_PASS)))
  {
    Serial.print(F("."));
    delay(500);
  }
  Serial.println(F("OK"));
  //setting up the ip configuration
  Fishino.config(ip, gateway, netmask);
  // wait for connection completion
  Serial.print(("Waiting IP..."));
  while(Fishino.status() != STATION_GOT_IP)
  {
    Serial.print(F("."));
    delay(500);
  }
  Serial.println(F("OK"));
  // print current IP address
  Serial.print(F("IP address: "));
  Serial.println(Fishino.localIP());
}

//function to initialize the RTC on board
void initRTC() {
  if (!RTC.isrunning()) {
    RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
}

//function to initialize the SD card
void initSD() {
  // initialize the SD card.
  Serial.print(F("Preparing SD..."));
  // Pass over the speed and Chip select for the SD card
  if (!card.init(SPI_FULL_SPEED, SD_CS)) {
    Serial.println(F("FAILED"));
    return;
  // initialize a FAT volume.
  } else if (!volume.init(&card)) {
    Serial.println(F("VOLUME FAILED"));
    return;
  
  } else if (!root.openRoot(&volume)) {
    Serial.println(F("ROOT FAILED"));
    return;
  }

  Serial.println("OK");
}

//function to initialize the web server
void initWebServer() {
  //register the handler for the web server
  //login and logout handlers
  web.addHandler(F("/login"), FishinoWebServer::POST, &loginHandler); //login
  web.addHandler(F("/logout"), FishinoWebServer::POST, &logoutHandler); //logout
  //index handler
  web.addHandler(F("/"), FishinoWebServer::GET, &indexHandler);
  //status handler
  web.addHandler(F("/status"), FishinoWebServer::GET, &statusHandler);
  //settings handlers
  web.addHandler(F("/settings/initialize"), FishinoWebServer::POST, &settingsInitializeHandler); //must be POST
  web.addHandler(F("/settings/selftest"), FishinoWebServer::POST, &settingsSelftestHandler); //must be POST
  web.addHandler(F("/settings/clear/d7s"), FishinoWebServer::POST, &settingsClearD7SHandler); //must be POST
  web.addHandler(F("/settings/clear/data"), FishinoWebServer::POST, &settingsClearDataHandler); //must be POST
  //getting data
  web.addHandler(F("/data"), FishinoWebServer::GET, &dataHandler);
  web.addHandler(F("/data/d7s"), FishinoWebServer::GET, &dataD7SHandler);
  //file handler
  web.addHandler(F("/" "*"), FishinoWebServer::GET, &fileHandler);

  //Request header "Cookie" to handle the session
  web.addHeader(F("Cookie"));
}

//function to initialize the D7S sensor
void initD7S() {
  //--- STARTING ---
  Serial.print(F("Starting D7S..."));
  //start D7S connection
  D7S.begin();
  //wait until the D7S is ready
  while (!D7S.isReady()) {
    Serial.print(F("."));
    delay(500);
  }
  Serial.println(F("OK"));

  //--- SETTINGS ---
  //setting the D7S to switch the axis at inizialization time
  D7S.setAxis(SWITCH_AT_INSTALLATION);

  //--- INTERRUPT SETTINGS ---
  //enabling interrupt INT1 on pin 2 of Arduino
  D7S.enableInterruptINT1(INT1_PIN);
  D7S.enableInterruptINT2(INT2_PIN);

  //registering event handler
  D7S.registerInterruptEventHandler(START_EARTHQUAKE, &startEarthquakeHandler); //START_EARTHQUAKE event handler
  D7S.registerInterruptEventHandler(END_EARTHQUAKE, &endEarthquakeHandler); //END_EARTHQUAKE event handler
  D7S.registerInterruptEventHandler(SHUTOFF_EVENT, &shutoffHandler); //SHUTOFF_EVENT event handler
  D7S.registerInterruptEventHandler(COLLAPSE_EVENT, &collapseHandler); //COLLAPSE_EVENT event handler

  //--- INITIALIZZAZION ---
  Serial.println(F("Initializing D7S."));
  //start the initial installation procedure
  D7S.initialize();
  //wait until the D7S is ready (the initializing process is ended)
  while (!D7S.isReady()) {
    Serial.print(F("."));
    delay(500);
  }
  Serial.println(F("OK"));

  //--- RESETTING EVENTS ---
  //reset the events shutoff/collapse memorized into the D7S
  D7S.resetEvents();

  //reset events and notifications of the fishmograph
  earthquake.events = 0x00; //reset events
  earthquake.notifications = 0x00; //reset notifications

  //--- STARTING INTERRUPT HANDLING ---
  D7S.startInterruptHandling();
}

void setup() {
  Serial.begin(9600);
  while(!Serial)
    ;

  //init fishino
  initFishino();
  //init RTC
  initRTC();
  //init SD
  //initSD();
  //init the web server
  initWebServer();
  //initialize the D7S sensor
  //initD7S();

  
  //the system is initialized, let's start the web server
  web.begin();

  //all ready to go
  Serial.println(F("Ready!\n"));

}

void loop() {
  //process the ingoing requests
  web.process();
}
