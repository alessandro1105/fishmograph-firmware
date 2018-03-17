#define DEBUG_LEVEL_FATAL
#define DEBUG_LEVEL_ERROR
#define DEBUG_LEVEL_WARNING
#define DEBUG_LEVEL_INFO

#include <FishinoWebServer.h>
#include <SD.h>
#include <D7S.h>
#include <FishinoRTC.h>
#include <ArduinoJson.h>
#include <SMTPClient.h>

//---------------- CONFIGURATION ----------------

//--- D7S ---
//D7S interrupt pin
#define INT1_PIN 3 //pin to which INT1 is attached
#define INT2_PIN 5 //pin to which INT2 is attached

//--- Network ---
//AP authentication
#define MY_SSID "ssid" //SSID
#define MY_PASS "password" //password
//IP Addresses
#define IPADDR  192, 168,   0, 200 //IP of the server
#define GATEWAY 192, 168,   0, 1 //Gateway of the network
#define NETMASK 255, 255, 255, 0 //Netmask of the network

//--- Webapp ---
//Webapp default password
#define WEBAPP_DEFAULT_PASSWORD "fish123";

//--- Email notifications ---
//Uncomment the line below to enable email notifications
#define ENABLE_EMAIL_NOTIFICATION
//SMTP server information
//Remember: if you use a server that require TLS you need a client capable secure connections
#define SMTP_SERVER "smtp.gmail.com" //SMTP server
#define SMTP_PORT 465 //SMTP server port
//SMTP autherntication
#define SMTP_LOGIN "your email encoded in base64" //username encoded in base64
#define SMTP_PASSWD "your password encoded in base64" //password encoded in base64
//Email sender info
#define SMTP_FROM_NAME "Fishmograph" //Sender name
#define SMTP_FROM_EMAIL "sender@example.ext" //Sender email

//--- General ---
//Time for which the notification is valid. Expired this time the notification will be not sent
#define NOTIFICATION_VALIDITY_TIME 120000 //time in ms (2 minutes)
#define EMAIL_NOTIFICATION_WAIT_TIME 10000 //time to wait in ms before check for email notifications to send


//---------------- DEBUG ----------------

//comment this to disable all debug information
#define DEBUG_OUTPUT

#ifdef DEBUG_OUTPUT
  #define DEBUG_INIT(x) Serial.begin(x); while(!Serial);
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
#else
  #define DEBUG_INIT(x)
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif


//---------------- DEBUG ----------------

//--- SD Ccrd ---
//SDCS pin
#ifdef SDCS
  #define SD_CS SDCS
#else
  #define SD_CS 4
#endif

//--- D7S status ---
//Status of the D7S sensor (used primarly in the webapp)
typedef enum status {
  STATUS_ERROR = -1,
  STATUS_OK = 1,
  EARTHQUAKE_OCCURING = 2,
  EARTHQUAKE_ENDED = 3,
  SHUTOFF_OCCURRED = 4,
  COLLAPSE_OCCURRED = 5
};


//---------------- VARIABLES ----------------

//--- Network ---
IPAddress ip(IPADDR);
IPAddress gateway(GATEWAY);
IPAddress netmask(NETMASK);

//--- SD card ---
Sd2Card card;
SdVolume volume;
SdFile root;
SdFile file;

//--- Webapp ---
//Variable used to handle the web session
long sessionID = -1; //identificator of the web session (only positive number are valid session ID)
bool isUserLogged = false; //if true the user is logged in
//Fishino web server object
FishinoWebServer web(80);

//--- Earthquake ---
//Status of the earthquake
struct earthquake_t {
  //current events
  struct events_t {
    bool occuring; //earthquake started
    bool end; //arthquake ended
    bool shutoff; //shutoff occured
    bool collapse; //collapse occured
  } events;
  //earthquake info
  long start_timestamp; //timestamp at which the eathquake started
  long end_timestamp; //timestamp at which the earthquake ended
  long shutoff_timestamp; //timestamp at which the shutoff event occured
  long collapse_timestamp; //timestamp at which the shutoff event occured
  float si; //SI at the end of the earthquake
  float pga; //PGA at the end of the earthquake
  float temperature; //Temperature at which the earthquakes is registered
  //variable used to save the earthquake info to the SD card
  bool saved;
} earthquake;

//--- Notifications ---
struct notification_t {
  //notification structure
  struct status_t {
    bool occuring; // Earthquake started
    bool end; //earthquake ended
    bool shutoff; //shutoff occured
    bool collapse; //collapse occured
  };
  //web notifications status
  struct status_t web;

  //If email notifications is enabled
  #ifdef ENABLE_EMAIL_NOTIFICATION
    //email notifications status
    struct status_t email;
  #endif
} notifications;

//--- Email notifications ---
//If email notifications is enabled
#ifdef ENABLE_EMAIL_NOTIFICATION
  //Fishino secure client to connet to the SMTP server
  FishinoSecureClient client;

  long lastCheckEmailNotifications = 0;
#endif


//---------------- RESET NOTIFICATIONS CODE SECTION ----------------
//Reset notification status
void resetNotifications() {

  //reset web notification
  notifications.web.occuring = false;
  notifications.web.end = false;
  notifications.web.shutoff = false;
  notifications.web.collapse = false;

  #ifdef ENABLE_EMAIL_NOTIFICATION
    //reset email notification
    notifications.email.occuring = false;
    notifications.email.end = false;
    notifications.email.shutoff = false;
    notifications.email.collapse = false;
  #endif

}

//---------------- D7S CODE SECTION ----------------

//Init the D7S sensor
void initD7S() {
  DEBUG_PRINTLN("--- Init D7S ----");
  
  DEBUG_PRINT(F("Starting D7S..."));
  //start D7S connection
  D7S.begin();
  //wait until the D7S is ready
  while (!D7S.isReady()) {
    DEBUG_PRINT(F("."));
    delay(500);
  }
  DEBUG_PRINTLN(F("OK"));

  //setting the D7S to switch the axis at inizialization time
  D7S.setAxis(SWITCH_AT_INSTALLATION);

  //setting D7S library interrupt handling
  D7S.enableInterruptINT1(INT1_PIN);
  D7S.enableInterruptINT2(INT2_PIN);

  //registering event handler
  D7S.registerInterruptEventHandler(START_EARTHQUAKE, &startEarthquakeHandler); //START_EARTHQUAKE event handler
  D7S.registerInterruptEventHandler(END_EARTHQUAKE, &endEarthquakeHandler); //END_EARTHQUAKE event handler
  D7S.registerInterruptEventHandler(SHUTOFF_EVENT, &shutoffHandler); //SHUTOFF_EVENT event handler
  D7S.registerInterruptEventHandler(COLLAPSE_EVENT, &collapseHandler); //COLLAPSE_EVENT event handler

  DEBUG_PRINTLN(F("Initializing the D7S sensor in 2 seconds. Keep it steady!"));
  delay(2000);

  //initialize the sensor
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

  //reset notifications status
  resetNotifications();

  //setting that the earthquake is saved to prevent false entry
  earthquake.saved = true;

  //start D7S library interrupt handling
  D7S.startInterruptHandling();
}

//Handler function for the earthquake begin event
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

  //reset notifications status
  resetNotifications();

  //setting that the current earthquakes is not saved in the sd card
  earthquake.saved = false;

  //debug information
  DEBUG_PRINTLN(F("Earthquake started!"));
}

//Handler function for the earthquake end event
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
  DEBUG_PRINTLN(F("Earthquake ended!"));
}

//Handler function for the shutoff condition
void shutoffHandler() {
  // Saving the shutoff event
  earthquake.shutoff_timestamp = RTC.now().getUnixTime();
  earthquake.events.shutoff = true;

  //debug information
  DEBUG_PRINTLN(F("Shutoff!"));
} 

//Handler function for the collapse condition
void collapseHandler() {
  // Saving the shutoff event
  earthquake.collapse_timestamp = RTC.now().getUnixTime();
  earthquake.events.collapse = true;
  
  //debug information
  DEBUG_PRINTLN(F("--- COLLAPSE ---"));
}


//---------------- FISHINO CODE SECTION ----------------

//Init Fishino board
void initFishino() {
  DEBUG_PRINTLN("--- Init Fishino ----");

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


//---------------- RTC CODE SECTION ----------------

//Init on board RTC
void initRTC() {
  DEBUG_PRINTLN("--- Init RTC ----");
  DEBUG_PRINT("Initializing...");

  if (!RTC.isrunning()) {
    RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  DEBUG_PRINTLN("OK");
}


//---------------- SD CARD CODE SECTION ----------------

//Init SD card
void initSD() {
  DEBUG_PRINTLN("--- Init SD Card ----");

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


//---------------- WEBAPP USER CODE SECTION ----------------

//Get the user password from a file
char *getUserPassword() {

  //if the password file doesn't exists use the default password
  if (!file.open(&root, "PASSWORD.TXT", O_READ | O_WRITE)) {
    return WEBAPP_DEFAULT_PASSWORD;
  }

  char password[21]; //max password is 20 char

  //read the password from the file
  int size = file.read(password, 20);
  password[size] = 0;

  file.close();

  //return the password
  return password;
}


//---------------- FISHINO WEBSERER CODE SECTION ----------------

//Init web server
void initWebServer() {
  DEBUG_PRINTLN("--- Init Web server ----");
  DEBUG_PRINT("Initializing...");
  //register the handler for the web server
  //login and logout handlers
  web.addHandler(F("/login"), FishinoWebServer::POST, &loginHandler); //login
  web.addHandler(F("/logout"), FishinoWebServer::POST, &logoutHandler); //logout
  web.addHandler(F("/password/update"), FishinoWebServer::POST, &passwordUpdateHandler); //change password
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
  //alerts handling
  web.addHandler(F("/recipient/list"), FishinoWebServer::GET, &recipientListHandler);
  web.addHandler(F("/recipient/new"), FishinoWebServer::POST, &recipientNewHandler);
  web.addHandler(F("/recipient/delete"), FishinoWebServer::POST, &recipientDeleteHandler);
  //file handler
  web.addHandler(F("/" "*"), FishinoWebServer::GET, &fileHandler);

  //Request header "Cookie" to handle the session
  web.addHeader(F("Cookie"));

  DEBUG_PRINTLN("OK");
}


//--- AUxiliary functions ---

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

//Send HTTP status code
//FishinoWebServer close the header section of the response if the status code is different from 200
void sendHTTPStatusCode(FishinoWebServer& web, uint16_t statusCode) {
  FishinoClient client = web.getClient();
  client << F("HTTP/1.1 ") << statusCode << F(" OK\r\n");
}

// sends a file to client
void sendFile(FishinoWebServer& web, const char* filename, const __FlashStringHelper *contentType, bool allowProtectedFile) {
  //getting the client
  FishinoClient& client = web.getClient();
  
  // if the filename is not valid
  if (!filename) {
    sendHTTPStatusCode(web, 404);
    web.endHeaders();
    client.println(F("Could not parse URL"));
    client.println();
    DEBUG_PRINTLN("Could not parse URL");
    return;
  }

  //if the file doesn't exists or it's protected
  if ((!allowProtectedFile && (strcmp(filename, "DATA.TXT") == 0 || strcmp(filename, "EMAILS.TXT") == 0 || strcmp(filename, "PASSWORD.TXT") == 0)) || !file.open(&root, filename, O_READ)) {
    sendHTTPStatusCode(web, 404);
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

//Start a web session
void startSession(FishinoWebServer &web) {
  //intialize the session cookie value
  long cookieSessionValue = -1;
  //read the session cookie
  const char *cookieSession = web.getHeaderValue("Cookie");
  //if the request has send a cookie
  if (cookieSession != 0) {
    cookieSessionValue = atoi(cookieSession +8);
  }
  //check if the value of the cookie is different from the session ID we are currently handling
  if (cookieSessionValue < 0 || cookieSessionValue != sessionID) {
    sessionID = RTC.now().getUnixTime(); //using timestamp for uniqness
    //reset isUserLogged session variable
    isUserLogged = false;
  }
}

//Send the web session cookie
void sendSessionCookie(FishinoWebServer &web) {
  //we need to send the "set-cookie" header with the cookie containing the session ID
  char cookie[50];
  sprintf(cookie, "session=%d; Path=/", sessionID);
  sendHTTPHeader(web, F("Set-Cookie"), cookie);
}


//--- Restful handlers ---

// "/"
bool indexHandler(FishinoWebServer &web) {
  sendFile(web, "index.htm", NULL, false);
  return true;
}

// "/login"
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

// "/logout"
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

// "/password/update"
bool passwordUpdateHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);

  //if the user if not logged we send a 401 - Unauthorized
  if (!isUserLogged) {
    sendHTTPStatusCode(web, 401);
    sendSessionCookie(web);
    web.endHeaders();
    return true;
  }

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

    //get the password from the request
    const char *password = request["password"];

    //open file
    file.open(&root, "PASSWORD.TXT", O_CREAT | O_WRITE | O_TRUNC);

    //save the password
    file.print(password);

    //close the file
    file.close();
  }

  //send the session "set-cookie" header
  sendSessionCookie(web);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  web.endHeaders();

  return true;
}

// "/status"
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
    response["timestamp"] = earthquake.collapse_timestamp;
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

// "/settings/initialize"
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

// "/settings/selftest"
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

// "/settings/clear/d7s"
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

// "/settings/clear/data"
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

  //creating a new file DATA.TXT
  file.open(&root, "DATA.TXT", O_CREAT | O_WRITE | O_TRUNC);
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

// "/data"
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

  //if the file doesn't exists
  if (!file.open(&root, "DATA.TXT", O_READ | O_WRITE)) {
    file.open(&root, "DATA.TXT", O_CREAT | O_WRITE | O_TRUNC);
    file.print(F("[]"));
  }

  file.close();

  //sending the file
  sendFile(web, "DATA.TXT", F("application/json"), true);
  
  return true;
}

// "/data/d7s"
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

// "/recipient/list"
bool recipientListHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);

  //if the user if not logged we send a 401 - Unauthorized
  if (!isUserLogged) {
    sendHTTPStatusCode(web, 401);
    sendSessionCookie(web);
    web.endHeaders();
    return true;
  }

  //if the file doesn't exists
  if (!file.open(&root, "EMAILS.TXT", O_READ | O_WRITE)) {
    file.open(&root, "EMAILS.TXT", O_CREAT | O_WRITE | O_TRUNC);
    file.print(F("[]"));
  }

  file.close();

  //sending the file
  sendFile(web, "EMAILS.TXT", F("application/json"), true);
  
  return true;
}

// "/recipient/delete"
bool recipientDeleteHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);

  //if the user if not logged we send a 401 - Unauthorized
  if (!isUserLogged) {
    sendHTTPStatusCode(web, 401);
    sendSessionCookie(web);
    web.endHeaders();
    return true;
  }

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
  if (!request.success() || !request.containsKey("index")) {
    sendHTTPStatusCode(web, 400);

  } else {

    //open file
    if (file.open(&root, "EMAILS.TXT", O_READ | O_WRITE)) {

      //read the entire file of the recipients
      char buffer[1024];

      //read the file
      int size = file.read(buffer, 1023);
      buffer[size] = 0;

      //interpret json
      StaticJsonBuffer<1024> jsonBuffer;
      JsonArray& recipients = jsonBuffer.parseArray(buffer);

      if (recipients.success()) {
        //remove the index
        recipients.remove((int) request["index"]);

        //close the file and create a new one
        file.close();
        file.open(&root, "EMAILS.TXT", O_CREAT | O_WRITE | O_TRUNC);

        //insert the new json
        recipients.printTo(file);

        //close the file
        file.close();

        sendHTTPStatusCode(web, 204);

      } else {
        sendHTTPStatusCode(web, 500);
      }
      
    } else {
      //send the session "set-cookie" header
      sendHTTPStatusCode(web, 500);
    }
  }

  //send the session "set-cookie" header
  sendSessionCookie(web);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  web.endHeaders();

  return true;
}

// "/recipient/new"
bool recipientNewHandler(FishinoWebServer &web) {
  //start the session handling
  startSession(web);

  //if the user if not logged we send a 401 - Unauthorized
  if (!isUserLogged) {
    sendHTTPStatusCode(web, 401);
    sendSessionCookie(web);
    web.endHeaders();
    return true;
  }

  FishinoClient client = web.getClient();
  //getting the body of the request
  char body[client.available() +1]; //we create a buffer of the correct size of the body +1 (the null terminator)
  int i = 0;
  while (client.available() && i < 300) {
    body[i++] = (char) client.read();
  }
  body[i] = 0;

  //parsing the request
  StaticJsonBuffer<400> jsonBuffer;
  JsonObject& request = jsonBuffer.parseObject(body);

  //the body of the request is incorrect
  if (!request.success() || !request.containsKey("name") || !request.containsKey("email")) {
    sendHTTPStatusCode(web, 400);

  } else {

    //copying name and email from the body of the request
    char name[41];
    char email[51];
    strcpy(name, (const char *) request["name"]);
    strcpy(email, (const char *) request["email"]);

    //if the file doesn't exists
    if (!file.open(&root, "EMAILS.TXT", O_READ | O_WRITE)) {
      file.open(&root, "EMAILS.TXT", O_CREAT | O_READ| O_WRITE | O_TRUNC);
      file.print(F("[]"));
      file.rewind();
    }

    //read the entire file of the alerts
    char buffer[1024];

    //read the file
    int size = file.read(buffer, 1023);
    buffer[size] = 0;

    //interpret json
    StaticJsonBuffer<1024> jsonBuffer2;
    JsonArray& recipients = jsonBuffer2.parseArray(buffer);

    if (recipients.success()) {
      JsonObject& recipient = recipients.createNestedObject();

      recipient["name"] = name;
      recipient["email"] = email;

      //write the alerts back to the file
      file.rewind();

      //insert the new json
      recipients.printTo(file);

      sendHTTPStatusCode(web, 204);
    } else {
      sendHTTPStatusCode(web, 500);
    }

    //close the file
    file.close();
  }

  //send the session "set-cookie" header
  sendSessionCookie(web);
  sendHTTPHeader(web, F("Content-Type"), F("application/json"));
  web.endHeaders();

  return true;
}

//File handler
bool fileHandler(FishinoWebServer &web) {
  //filename of the file that we need to serve
  const char *filename = web.getFileFromPath(web.getPath()).c_str();

  sendFile(web, filename, NULL, false);

  return true;
}


//---------------- EMAIL NOTIFICATIONS CODE SECTION ----------------

//Add this code only if notification by email is enabled
#ifdef ENABLE_EMAIL_NOTIFICATION

  //Send email notifications
  void sendEmailNotifications() {

    long now = RTC.now().getUnixTime();

    //check if there are notification to be sent
    if ((earthquake.events.occuring && !notifications.email.occuring) && (now - earthquake.start_timestamp) <= NOTIFICATION_VALIDITY_TIME ||
        (earthquake.events.occuring && earthquake.events.shutoff && !notifications.email.shutoff) && (now - earthquake.shutoff_timestamp) <= NOTIFICATION_VALIDITY_TIME ||
        (earthquake.events.occuring && earthquake.events.collapse && !notifications.email.collapse) && (now - earthquake.collapse_timestamp) <= NOTIFICATION_VALIDITY_TIME ||
        (earthquake.events.end && !notifications.email.end) && (now - earthquake.end_timestamp) <= NOTIFICATION_VALIDITY_TIME) {

      SMTPClient smtp(client, SMTP_SERVER, SMTP_PORT);
      // Setting the AUTH LOGIN information
      smtp.setAuthLogin(SMTP_LOGIN, SMTP_PASSWD);

      // Creating an email container
      Mail mail;

      // Compiling the email
      mail.from(SMTP_FROM_EMAIL, SMTP_FROM_NAME);


      //if the file doesn't exists
      if (!file.open(&root, "EMAILS.TXT", O_READ)) {
        file.open(&root, "EMAILS.TXT", O_CREAT | O_WRITE | O_TRUNC);
        file.print(F("[]"));
        file.close();
        return; //no alerts
      }

      //read the entire file of the alerts
      char buffer[1024];

      //read the file
      int size = file.read(buffer, 1023);
      buffer[size] = 0;

      //closing file
      file.close();

      //interpret json
      StaticJsonBuffer<1024> jsonBuffer;
      JsonArray& recipients = jsonBuffer.parseArray(buffer);

      //file not valid
      if (!recipients.success()) {
        DEBUG_PRINTLN("File EMAILS.TXT error!");
        return;
      }

      //no recipients
      if (recipients.size() == 0) {
        return;
      }

      //recipients of the email alert
      //setting the recipients
      for (auto recipient : recipients) {
        mail.to((const char *) recipient["email"], (const char *) recipient["name"]);
      }

      //Subject and message
      //earthquake is occuring and the notifications has not been sent before
      if (earthquake.events.occuring && !notifications.email.occuring) {
        //email subject
        mail.subject("Fishmograph: Earthquake started");
        //body of the mail
        char body[100];
        //create the date
        DateTime date(earthquake.start_timestamp, DateTime::EPOCH_UNIX);
        sprintf(body, "An earthquake started at %02d/%02d/%04d %02d:%02d:%02d!", date.month(), date.day(), date.year(), date.hour(), date.minute(), date.second());
        mail.body(body);
        
        //setting that the notification has been sent
        notifications.email.occuring = true;
      
      //earthquake is occuring and the shutoff notifications has not been sent before
      } else if (earthquake.events.occuring && earthquake.events.shutoff && !notifications.email.shutoff) {
        //email subject
        mail.subject("Fishmograph: Shutoff signal emitted");
        //body of the mail
        char body[100];
        //create the date
        DateTime date(earthquake.shutoff_timestamp, DateTime::EPOCH_UNIX);
        sprintf(body, "Shutoff signal emitted at %02d/%02d/%04d %02d:%02d:%02d!", date.month(), date.day(), date.year(), date.hour(), date.minute(), date.second());
        mail.body(body);
        //setting that the notification has been sent
        notifications.email.shutoff = true;

      //earthquake is occuring and the collapse notifications has not been sent before
      } else if (earthquake.events.occuring && earthquake.events.collapse && !notifications.email.collapse) {
        //email subject
        mail.subject("Fishmograph: Collapse signal emitted");
        //body of the mail
        char body[100];
        //create the date
        DateTime date(earthquake.collapse_timestamp, DateTime::EPOCH_UNIX);
        sprintf(body, "Collapse signal emitted at %02d/%02d/%04d %02d:%02d:%02d!", date.month(), date.day(), date.year(), date.hour(), date.minute(), date.second());
        mail.body(body);
        //setting that the notification has been sent
        notifications.email.collapse = true;
      
      //earthquake is endend and the notifications has not been sent before and there is still time to sent it
      } else if (earthquake.events.end && !notifications.email.end) {
        //email subject
        mail.subject("Fishmograph: Earthquake ended");
        //body of the mail
        char body[100];
        sprintf(body, "The earthquake ended! si: %.2f [m/s], pga: %.2f [m/s^2], temperature: %.2f [C]", earthquake.si, earthquake.pga, earthquake.temperature);
        mail.body(body);
        //setting that the notification has been sent
        notifications.email.end = true;
      
      }

      // Send the email through the SMTP client
      if (smtp.send(mail) != SMTP_OK) {
        DEBUG_PRINTLN("SMTP server problem!");
      }

    }
    
  }

#endif


//---------------- EARTHQUAKE INFO SAVING CODE SECTION ----------------

//Save the earthquake info to the SD card
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
      detection["collapse"] = earthquake.collapse_timestamp;
    } else {
      detection["collapse"] = 0;
    }

    //position to last data
    unsigned long position = 0;
    //open file
    if (!file.open(&root, "DATA.TXT", O_READ | O_WRITE)) {
      file.open(&root, "DATA.TXT", O_CREAT | O_WRITE | O_TRUNC);
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
  //Initialize the debug serial
  DEBUG_INIT(9600);

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
  DEBUG_PRINTLN(F("\nFishmograph ready!\n"));
}

void loop() {
  //process the ingoing requests
  web.process();

  //save the earthquake data if new
  saveEarthquakeData();

  #ifdef ENABLE_EMAIL_NOTIFICATION
    //send email notifications
    if (millis() > lastCheckEmailNotifications + EMAIL_NOTIFICATION_WAIT_TIME) {
      sendEmailNotifications();
      lastCheckEmailNotifications = millis();
    }
  #endif
}
