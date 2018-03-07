#include <FishinoWebServer.h>
#include <SD.h>
#include <D7S.h>
#include <FishinoRTC.h>
#include <ArduinoJson.h>

//---------------- DEBUG ----------------
//comment this to disable all debug information
#define DEBUG

#ifdef DEBUG
  #define DEBUG_PRINT(x) Serial.print(x);
  #define DEBUG_PRINTLN(x) Serial.println(x);
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

//---------------- CONSTANTS ----------------
//WEBAPP PASSWORD
#define WEBAPP_PASSWORD "fish123";

//NETWORKS
//put here your network settings
#define MY_SSID "Casa" //ssid
#define MY_PASS "alepas1105119" //password

//IP ADDRESS CONSTANTS
#define IPADDR  192, 168,   0, 200 //ip of the server
#define GATEWAY 192, 168,   0, 1 //gateway of the network
#define NETMASK 255, 255, 255, 0 //netmask of the network

//SD CARD
#ifdef SDCS
  #define SD_CS SDCS
#else
  #define SD_CS 4
#endif

//D7S INTERRUPT PINS
#define INT1_PIN 3 //interrupt pin INT1 of D7S attached to pin 2 of Arduino
#define INT2_PIN 5 //interrupt pin INT2 of D7S attached to pin 3 of Arduino

//TIME FOR WHICH THE NOTIFICATION IS VALID
#define NOTIFICATION_VALIDITY_TIME 120000 //time in ms (2 minutes)

typedef enum status {
  STATUS_ERROR = -1,
  STATUS_OK = 1,
  EARTHQUAKE_OCCURING = 2,
  EARTHQUAKE_ENDED = 3,
  SHUTOFF_OCCURRED = 4,
  COLLAPSE_OCCURRED = 5
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
  // Events
  struct events_t {
    bool occuring; // Earthquake started
    bool end; //earthquake ended
    bool shutoff; //shutoff occured
    bool collapse; //collapse occured
  } events;
  // Earthquake information
  long start_timestamp; //timestamp at which the eathquake started
  long end_timestamp; //timestamp at which the earthquake ended
  long shutoff_timestamp; //timestamp at which the shutoff event occured
  long collapse_time; //timestamp at which the shutoff event occured
  float si;
  float pga;
  float temperature;
  bool saved;
} earthquake;

//Notifications
struct notification_t {
  // Web notifications
  struct status_t {
    bool occuring; // Earthquake started
    bool end; //earthquake ended
    bool shutoff; //shutoff occured
    bool collapse; //collapse occured
  };
  struct status_t web;
} notifications;

//---------------- D7S EVENTS HANDLERS ----------------
//handler for the START_EARTHQUAKE D7S event
void startEarthquakeHandler() {  
  // A new earthquake is detected
  // We need to clear notifications and events status and register the timestamp
  
  //timestamp of the earthquake start
  earthquake.start_timestamp = RTC.now().getUnixTime(); //save the timestamp

  //reset the events of the D7S
  D7S.resetEvents();
 
  //reset earthquake events
  earthquake.events.occuring = true;
  earthquake.events.end = false;
  earthquake.events.shutoff = false;
  earthquake.events.collapse = false;

  //reset web notification
  notifications.web.occuring = false;
  notifications.web.end = false;
  notifications.web.shutoff = false;
  notifications.web.collapse = false;

  //setting that the current earthquakes is not saved in the sd card
  earthquake.saved = false;

  //debug information
  DEBUG_PRINTLN(F("--- EARTHQUAKE STARTED ---"));
}

//handler for the END_EARTHQUAKE D7S event
void endEarthquakeHandler(float si, float pga, float temperature) {
  // The earthquake is ended
  // We need to save the data to the data file and save the timestamp

  //saving the timestamp at which the earthquakes is ended
  earthquake.end_timestamp = RTC.now().getUnixTime();

  //setting the events
  earthquake.events.end = true; // earthquake ended
  earthquake.events.occuring = false; //earthquake is not occuring because it's ended

  //saving the earthquake data
  earthquake.si = si;
  earthquake.pga = pga;
  earthquake.temperature = temperature;

  //debug information
  DEBUG_PRINTLN(F("--- EARTHQUAKE ENDED ---"));
}

//handler for the SHUTOFF D7S event
void shutoffHandler() {
  // Saving the shutoff event
  earthquake.shutoff_timestamp = RTC.now().getUnixTime();
  earthquake.events.shutoff = true;

  //debug information
  DEBUG_PRINTLN(F("--- SHUTOFF ---"));
} 

//handler for the COLLAPSE D7S event
void collapseHandler() {
  // Saving the shutoff event
  earthquake.collapse_time = RTC.now().getUnixTime();
  earthquake.events.collapse = true;
  
  //debug information
  DEBUG_PRINTLN(F("--- COLLAPSE ---"));
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
    // If the browser sent a valid cookie
    if (cookieSessionValue > 0) {
      sessionID = cookieSessionValue;
    } else {
      //we need to generate a new sessionID
      sessionID = RTC.now().getUnixTime(); //using timestamp for uniqness
    }
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
  return WEBAPP_PASSWORD;
}

//---------------- WEB SERVER HANDLERS ----------------
//handler for the url GET "/"
bool indexHandler(FishinoWebServer &web) {
  sendFile(web, "index.htm", NULL);
  return true;
}

//handler for the url POST "/login"
bool loginHandler(FishinoWebServer &web) {
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
    JsonObject& request = jsonBuffer.parseObject(body);

    //the body of the request is incorrect
    if (!request.success() || !request.containsKey("password")) {
      sendHTTPStatusCode(web, 400);

    } else {
      //getting the password from the body
      const char *password = request["password"];

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
  //loggin out the user
  isUserLogged = false;
  //send that the procedure goes correctly
  sendHTTPStatusCode(web, 204);
  //send the session "set-cookie" header
  sendSessionCookie(web);
  web.endHeaders();
  return true;
}

//handler for the url GET "/status"
bool statusHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);

  //if the user if not logged we send a 401 - Unauthorized
  if (!isUserLogged) {
    sendHTTPStatusCode(web, 401);
    sendSessionCookie(web);
    web.endHeaders();
    return true;
  }

  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  sendSessionCookie(web);
  web.endHeaders();

  //prepare the body of the response
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& response = jsonBuffer.createObject();

  //earthquake is occuring and the notifications has not been sent before
  if (earthquake.events.occuring && !notifications.web.occuring) {
    response["status"] = (int) EARTHQUAKE_OCCURING;
    response["timestamp"] = earthquake.start_timestamp;
    //setting that the notification has been sent
    notifications.web.occuring = true;
  
  //earthquake is occuring and the shutoff notifications has not been sent before
  } else if (earthquake.events.occuring && earthquake.events.shutoff && !notifications.web.shutoff) {
    response["status"] = (int) SHUTOFF_OCCURRED;
    response["timestamp"] = earthquake.shutoff_timestamp;
    //setting that the notification has been sent
    notifications.web.shutoff = true;

  //earthquake is occuring and the collapse notifications has not been sent before
  } else if (earthquake.events.occuring && earthquake.events.collapse && !notifications.web.collapse) {
    response["status"] = (int) COLLAPSE_OCCURRED;
    response["timestamp"] = earthquake.collapse_time;
    //setting that the notification has been sent
    notifications.web.collapse = true;
  
  //earthquake is endend and the notifications has not been sent before and there is still time to sent it
  } else if (earthquake.events.end && (RTC.now().getUnixTime() - earthquake.end_timestamp) <= NOTIFICATION_VALIDITY_TIME && !notifications.web.end) {
    response["status"] = (int) EARTHQUAKE_ENDED;
    response["timestamp"] = earthquake.end_timestamp;
    //setting that the notification has been sent
    notifications.web.end = true;
  
  } else {
    response["status"] = (int) STATUS_OK;
  }

  //getting the client
  FishinoClient& client = web.getClient();

  //send the response
  response.printTo(client);
  //close the body
  client.println();

  return true;
}

//handler for the url POST "/settings/initialize"
bool settingsInitializeHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);

  //if the user if not logged we send a 401 - Unauthorized
  if (!isUserLogged) {
    sendHTTPStatusCode(web, 401);
    sendSessionCookie(web);
    web.endHeaders();
    return true;
  }

  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  sendSessionCookie(web);
  web.endHeaders();

  //prepare the body of the response
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& response = jsonBuffer.createObject();

  //if an earthquake is occuring we cannot initialize the sensor
  if (earthquake.events.occuring) {
    response["status"] = (int) EARTHQUAKE_OCCURING;
  
  } else {
    //disable D7S interrupt handling
    D7S.stopInterruptHandling();
    DEBUG_PRINT(F("Initializing..."));
    //start the initial installation procedure
    D7S.initialize();
    //wait until the D7S is ready (the initializing process is ended)
    while (!D7S.isReady()) {
      DEBUG_PRINT(F("."));
      delay(500);
    }
    DEBUG_PRINTLN(F("OK"));
    //enabling D7S interrupt handling
    D7S.startInterruptHandling();

    response["status"] = (int) STATUS_OK;
  }

  //getting the client
  FishinoClient& client = web.getClient();

  //send the response
  response.printTo(client);
  //close the body
  client.println();

  return true;
}

//handler for the url POST "/settings/selftest"
bool settingsSelftestHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);

  //if the user if not logged we send a 401 - Unauthorized
  if (!isUserLogged) {
    sendHTTPStatusCode(web, 401);
    sendSessionCookie(web);
    web.endHeaders();
    return true;
  }

  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  sendSessionCookie(web);
  web.endHeaders();

  //prepare the body of the response
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& response = jsonBuffer.createObject();

  //if an earthquake is occuring we cannot initialize the sensor
  if (earthquake.events.occuring) {
    response["status"] = (int) EARTHQUAKE_OCCURING;
  
  } else {
    //disable D7S interrupt handling
    D7S.stopInterruptHandling();

    DEBUG_PRINT(F("Selftest..."));
    D7S.selftest();
    //wait until the D7S selftest is ended
    while (!D7S.isReady()) {
      DEBUG_PRINT(F("."));
      delay(500);
    }
    //checking the result
    if (D7S.getSelftestResult() == D7S_OK) {
      response["status"] = (int) STATUS_OK;
      DEBUG_PRINTLN(F("OK"));
    } else {
      response["status"] = (int) STATUS_ERROR;
      DEBUG_PRINTLN(F("ERROR"));
    }

    //enabling D7S interrupt handling
    D7S.startInterruptHandling();
  }

  //getting the client
  FishinoClient& client = web.getClient();

  //send the response
  response.printTo(client);
  //close the body
  client.println();

  return true;
}

//handler for the url POST "/settings/clear/d7s"
bool settingsClearD7SHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);

  //if the user if not logged we send a 401 - Unauthorized
  if (!isUserLogged) {
    sendHTTPStatusCode(web, 401);
    sendSessionCookie(web);
    web.endHeaders();
    return true;
  }

  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  sendSessionCookie(web);
  web.endHeaders();

  //clearing the memory of D7S
  D7S.clearEarthquakeData();

  //prepare the body of the response
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& response = jsonBuffer.createObject();

  // prepare the response
  response["status"] = (int) STATUS_OK;

  //getting the client
  FishinoClient& client = web.getClient();

  //send the response
  response.printTo(client);
  //close the body
  client.println();


  return true;
}

//handler for the url POST "/settings/clear/data"
bool settingsClearDataHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);

  //if the user if not logged we send a 401 - Unauthorized
  if (!isUserLogged) {
    sendHTTPStatusCode(web, 401);
    sendSessionCookie(web);
    web.endHeaders();
    return true;
  }

  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  sendSessionCookie(web);
  web.endHeaders();

  //creating a new file data.txt
  file.open(&root, "data.txt", O_CREAT | O_WRITE | O_TRUNC);
  file.print(F("[]"));
  file.close();

  //prepare the body of the response
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& response = jsonBuffer.createObject();

  // prepare the response
  response["status"] = (int) STATUS_OK;

  //getting the client
  FishinoClient& client = web.getClient();

  //send the response
  response.printTo(client);
  //close the body
  client.println();

  return true;
}

//handler for the url GET "/data/earthquakes"
bool dataHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);

  //if the user if not logged we send a 401 - Unauthorized
  if (!isUserLogged) {
    sendHTTPStatusCode(web, 401);
    sendSessionCookie(web);
    web.endHeaders();
    return true;
  }

  //sending the file
  sendFile(web, "data.txt", F("application/json"));
  
  return true;
}

//handler for the url GET "/data/d7s"
bool dataD7SHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);

  //if the user if not logged we send a 401 - Unauthorized
  if (!isUserLogged) {
    sendHTTPStatusCode(web, 401);
    sendSessionCookie(web);
    web.endHeaders();
    return true;
  }

  //send headers
  sendHTTPStatusCode(web, 200);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  sendSessionCookie(web);
  web.endHeaders();

  //prepare the body of the response
  StaticJsonBuffer<1000> jsonBuffer;
  JsonArray& response = jsonBuffer.createArray();

  //ranked data
  JsonArray& ranked = response.createNestedArray();

  for (int i = 0; i < 5; i++) {
    JsonObject& detection = ranked.createNestedObject();

    detection["si"] = D7S.getRankedSI(i);
    detection["pga"] = D7S.getRankedPGA(i);
    detection["temperature"] = D7S.getRankedTemperature(i);
  }

  //lastest data
  JsonArray& lastest = response.createNestedArray();

  for (int i = 0; i < 5; i++) {
    JsonObject& detection = lastest.createNestedObject();

    detection["si"] = D7S.getLastestSI(i);
    detection["pga"] = D7S.getLastestPGA(i);
    detection["temperature"] = D7S.getLastestTemperature(i);
  }


  //getting the client
  FishinoClient& client = web.getClient();

  //send the response
  response.printTo(client);
  //close the body
  client.println();

  return true;
}

//handler for the url GET "/" "*"
bool fileHandler(FishinoWebServer &web) {
  //filename of the file that we need to serve
  const char *filename = web.getFileFromPath(web.getPath()).c_str();

  sendFile(web, filename, NULL);

  return true;
}

//---------------- WEBSERVER UTILS FUNCTIONS ----------------
// sends a file to client
void sendFile(FishinoWebServer& web, const char* filename, const __FlashStringHelper *contentType) {
  //start the session handling
  startSession(web);

  //getting the client
  FishinoClient& client = web.getClient();
  
  // if the filename is not valid
  if (!filename) {
    sendHTTPStatusCode(web, 404);
    sendSessionCookie(web);
    web.endHeaders();
    client.println(F("Could not parse URL"));
    client.println();
    DEBUG_PRINTLN("Could not parse URL");
    return;
  }

  //if the file doesn't exists
  if (!file.open(&root, filename, O_READ)) {
    sendHTTPStatusCode(web, 404);
    sendSessionCookie(web);
    web.endHeaders();
    client.print(F("Could not find file: "));
    client.println(filename);
    client.println();
    DEBUG_PRINT(F("Could not find file: "));
    DEBUG_PRINTLN(filename);
    return;
  }

  //the file exists and can be sent
  sendHTTPStatusCode(web, 200);
  sendSessionCookie(web);

  //svg type
  if (contentType != NULL) {
    sendHTTPHeader(web, F("Content-Type"), contentType);

  } else if (strstr(filename, ".SVG") != NULL) {
    sendHTTPHeader(web, F("Content-Type"), F("image/svg+xml"));
    sendHTTPHeader(web, F("Cache-Control"), F("public, max-age=900"));

  //other types
  } else {
    FishinoWebServer::MimeType mimeType = web.getMimeTypeFromFilename(filename);
    web.sendContentType(mimeType);

    if (mimeType == FishinoWebServer::MIMETYPE_GIF ||
        mimeType == FishinoWebServer::MIMETYPE_JPG ||
        mimeType == FishinoWebServer::MIMETYPE_PNG ||
        mimeType == FishinoWebServer::MIMETYPE_ICO) {
      sendHTTPHeader(web, F("Cache-Control"), F("public, max-age=900"));
    }
  }

  web.endHeaders();
  //send the file
  web.sendFile(file);
  //close the file
  file.close();
}

//auxiliary function to send HTTP headers
void sendHTTPHeader(FishinoWebServer& web, const __FlashStringHelper *header, const __FlashStringHelper *value) {
  FishinoClient client = web.getClient();
  client.print(header);
  client.print(": ");
  client.println(value);
}

//auxiliary function to send HTTP headers
void sendHTTPHeader(FishinoWebServer& web, const __FlashStringHelper *header, const char *value) {
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
  DEBUG_PRINT(F("Resetting Fishino..."));
  while(!Fishino.reset())
  {
    DEBUG_PRINT(F("."));
    delay(500);
  }
  DEBUG_PRINTLN(F("OK"));
  //Connetiing Fishino to the WiFi
  Fishino.setMode(STATION_MODE);
  DEBUG_PRINT(F("Connecting AP..."));
  while(!Fishino.begin(F(MY_SSID), F(MY_PASS)))
  {
    DEBUG_PRINT(F("."));
    delay(500);
  }
  DEBUG_PRINTLN(F("OK"));
  //setting up the ip configuration
  Fishino.config(ip, gateway, netmask);
  // wait for connection completion
  DEBUG_PRINT(("Waiting IP..."));
  while(Fishino.status() != STATION_GOT_IP)
  {
    DEBUG_PRINT(F("."));
    delay(500);
  }
  DEBUG_PRINTLN(F("OK"));
  // print current IP address
  DEBUG_PRINT(F("IP address: "));
  DEBUG_PRINTLN(Fishino.localIP());
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
  DEBUG_PRINT(F("Preparing SD..."));
  // Pass over the speed and Chip select for the SD card
  if (!card.init(SPI_FULL_SPEED, SD_CS)) {
    DEBUG_PRINTLN(F("FAILED"));
    return;
  // initialize a FAT volume.
  } else if (!volume.init(&card)) {
    DEBUG_PRINTLN(F("VOLUME FAILED"));
    return;
  
  } else if (!root.openRoot(&volume)) {
    DEBUG_PRINTLN(F("ROOT FAILED"));
    return;
  }

  DEBUG_PRINTLN("OK");
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
  DEBUG_PRINT(F("Starting D7S..."));
  //start D7S connection
  D7S.begin();
  //wait until the D7S is ready
  while (!D7S.isReady()) {
    DEBUG_PRINT(F("."));
    delay(500);
  }
  DEBUG_PRINTLN(F("OK"));

  //--- SETTINGS ---
  //setting the D7S to switch the axis at inizialization time
  D7S.setAxis(SWITCH_AT_INSTALLATION);

  //--- INTERRUPT SETTINGS ---
  //enabling interrupt INT1 on pin 2 of Arduino
  D7S.enableInterruptINT1(INT1_PIN);
  D7S.enableInterruptINT2(INT2_PIN);

  // //registering event handler
  D7S.registerInterruptEventHandler(START_EARTHQUAKE, &startEarthquakeHandler); //START_EARTHQUAKE event handler
  D7S.registerInterruptEventHandler(END_EARTHQUAKE, &endEarthquakeHandler); //END_EARTHQUAKE event handler
  D7S.registerInterruptEventHandler(SHUTOFF_EVENT, &shutoffHandler); //SHUTOFF_EVENT event handler
  D7S.registerInterruptEventHandler(COLLAPSE_EVENT, &collapseHandler); //COLLAPSE_EVENT event handler

  DEBUG_PRINTLN(F("Initializing the D7S sensor in 2 seconds. Keep it steady!"));
  delay(2000);

  //--- INITIALIZZAZION ---
  DEBUG_PRINT(F("Initializing D7S."));
  //start the initial installation procedure
  D7S.initialize();
  //wait until the D7S is ready (the initializing process is ended)
  while (!D7S.isReady()) {
    DEBUG_PRINT(F("."));
    delay(500);
  }
  DEBUG_PRINTLN(F("OK"));

  //--- RESETTING EVENTS ---
  //reset the events shutoff/collapse memorized into the D7S
  D7S.resetEvents();

  //reset earthquake events
  earthquake.events.occuring = false;
  earthquake.events.end = false;
  earthquake.events.shutoff = false;
  earthquake.events.collapse = false;

  //reset web notification
  notifications.web.occuring = false;
  notifications.web.end = false;
  notifications.web.shutoff = false;
  notifications.web.collapse = false;

  //setting that the earthquake is saved to prevent false entry
  earthquake.saved = true;


  //--- STARTING INTERRUPT HANDLING ---
  D7S.startInterruptHandling();
}

//---------------- UTILS FUNCTIONS ----------------
void saveEarthquakeData() {
  if (earthquake.events.end && !earthquake.saved) {
    //preparing the  detection to write to the SD card
    //parsing the request
    StaticJsonBuffer<200> jsonBuffer;
    JsonObject& detection = jsonBuffer.createObject();

    detection["start"] = earthquake.start_timestamp; //start timestamp
    detection["end"] = earthquake.start_timestamp; //end timestamp
    detection["si"] = earthquake.si; //si detected
    detection["pga"] = earthquake.pga; //pga detected
    detection["temperature"] = earthquake.temperature; //temperature at detection
    //if the shutoff event is occured
    if (earthquake.events.shutoff) {
      detection["shutoff"] = earthquake.shutoff_timestamp;
    } else {
      detection["shutoff"] = 0;
    }
    //if the collapse event is occured
    if (earthquake.events.collapse) {
      detection["collapse"] = earthquake.collapse_time;
    } else {
      detection["collapse"] = 0;
    }

    //position to last data
    unsigned long position = 0;
    //open file
    if (!file.open(&root, "data.txt", O_READ | O_WRITE)) {
      file.open(&root, "data.txt", O_CREAT | O_WRITE | O_TRUNC);
    }

    //counting the data
    char c;
    while ((c = file.read()) != -1) {
      if (c == '}') {
        position = file.curPosition();
      }
    }
    
    //if is the first data
    if (position == 0) {
      file.rewind();
      file.print(F("["));

    //there are other data
    } else {
      //reset file to the last postion
      file.seekSet(position);
      file.print(F(", "));
    }

    detection.printTo(file);
    file.print(F("]"));

    //close the file
    file.close();

    //setting that the earthquake data has been saved
    earthquake.saved = true;

  }
}

//---------------- SETUP AND LOOP ---------------- 
void setup() {
  Serial.begin(9600);
  while(!Serial)
    ;

  //init fishino
  initFishino();
  //init RTC
  initRTC();
  //init SD
  initSD();
  //init the web server
  initWebServer();
  //initialize the D7S sensor
  initD7S();

  //the system is initialized, let's start the web server
  web.begin();

  //all ready to go
  DEBUG_PRINTLN(F("Ready!\n"));

}

void loop() {
  //process the ingoing requests
  web.process();

  //save the earthquake data if new
  saveEarthquakeData();
}
