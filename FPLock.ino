const String ssid = "FPLock";      // SSID string
const String passwd = "414666888"; // WiFi password string

#define SERVO_PIN 4
#define SW_PIN 5
#define T_PIN 13

#include <Bounce2.h>
#include <Servo.h>
#include <FPC1020.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <LittleFS.h>

struct conf
{
  byte Open_Angle;              // in degree 0-180
  byte Close_Angle;             // in degree 0-180
  unsigned long Open_Dur;       // in ms (Can't be disabled)
  unsigned long Enroll_Timeout; // in ms (Can't be disabled)
  unsigned int Wait_For_Finger; // in ms (0 to disable)
} cfg;

Bounce BUTTON;
Servo HANDLE;
FPC1020 FPSensor; // 19200 BAUD rate by default
IPAddress apIP(192, 168, 0, 1);
DNSServer dnsServer;
ESP8266WebServer webServer(80);
extern unsigned char l_ucFPID;

unsigned long openTimer = 0;
unsigned long enrollTimer = 0;
byte enroll_state = 0;

void wifiOff();
void showArgList();
void enrollFP();
void cancelEnroll();
void confirmClear();
void clearFP();
void showIDnum();
void setHandler();
void gotoSleep();
void openTheDoor();
void setCfg();

void setup()
{
  cfg.Open_Angle = 70;
  cfg.Close_Angle = 0;
  cfg.Open_Dur = 3000;
  cfg.Wait_For_Finger = 100;
  cfg.Enroll_Timeout = 30000;

  LittleFS.begin();
  if (LittleFS.exists("/cfg.bin"))
  {
    File f = LittleFS.open("/cfg.bin", "r+");
    if (f)
    {
      if (f.size() == sizeof(struct conf))
        f.read((unsigned char*)&cfg, sizeof(struct conf));
      else
      {
        f.close();
        LittleFS.remove("/cfg.bin");
        f = LittleFS.open("/cfg.bin", "w+");
        f.write((unsigned char*)&cfg, sizeof(struct conf));
      }
      f.close();
    }
  }
  else
  {
    File f = LittleFS.open("/cfg.bin", "w+");
    if (f)
    {
      f.write((unsigned char*)&cfg, sizeof(struct conf));
      f.close();
    }
  }

  //pinMode(LED_BUILTIN, OUTPUT); // Will cause WiFi failure.
  pinMode(T_PIN, INPUT);
  BUTTON.attach(SW_PIN, INPUT);
  BUTTON.interval(25);                                  // Debounce interval 25ms
  HANDLE.attach(SERVO_PIN, 500, 2500, cfg.Close_Angle); // 0.5ms-2.5ms, return to Close_Angle
  //digitalWrite(LED_BUILTIN, HIGH);

  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, passwd, 13, false, 2); // channel 13, show SSID, max connection 2
  dnsServer.start(53, "*", apIP);          // on port 53, take any requests
  ArduinoOTA.setHostname(ssid.c_str());
  ArduinoOTA.begin(); // Default port 8266

  webServer.on("/", showArgList);
  webServer.on("/home", showArgList);
  webServer.on("/num", showIDnum);
  webServer.on("/set", setHandler);
  webServer.on("/sleep", gotoSleep);
  webServer.on("/enroll", enrollFP);
  webServer.on("/cancel", cancelEnroll);
  webServer.on("/confirm", confirmClear);
  webServer.on("/clear", clearFP);
  webServer.on("/wifioff", wifiOff);
  webServer.on("/open", openTheDoor);
  webServer.on("/cfg", setCfg);
  webServer.onNotFound(showArgList); // called when handler is not assigned

  webServer.begin();
  delay(1000); // Wait for TTP223
}

void loop()
{
  if (openTimer != 0) // The door is open
  {
    if (millis() - openTimer > cfg.Open_Dur)
    {
      HANDLE.write(cfg.Close_Angle); // Shut the door
      openTimer = 0;                 // Reset openTimer
    }
  }
  if (enrollTimer != 0) // Enroll processing
  {
    if (millis() - enrollTimer > cfg.Enroll_Timeout) // Enroll timeout
    {
      enrollTimer = 0;
      enroll_state = 0; // Reset enroll state
    }
  }

  BUTTON.update();
  if (BUTTON.rose()) // Button pressed
  {
    HANDLE.write(cfg.Open_Angle); // Open the door
    openTimer = millis();         // Set openTimer to now
  }

  if (digitalRead(T_PIN) == HIGH && enroll_state == 0 && openTimer == 0)
  {
    //digitalWrite(LED_BUILTIN, LOW);
    if (cfg.Wait_For_Finger != 0)
      delay(cfg.Wait_For_Finger);
    if (FPSensor.Search() == true) // Must be compared with true (1)
    {
      HANDLE.write(cfg.Open_Angle); // Open the door
      openTimer = millis();         // Set openTimer to now
    }
    //else
    //HANDLE.write(cfg.Close_Angle); // Only in case, not affecting openTimer
    //digitalWrite(LED_BUILTIN, HIGH);
  }

  dnsServer.processNextRequest();
  webServer.handleClient();
  ArduinoOTA.handle();
}

void wifiOff()
{
  WiFi.softAPdisconnect(true);
}

void showArgList()
{
  webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a href=\"enroll\">Enroll</a><br><a href=\"confirm\">Clear</a><br><a href=\"wifioff\">WiFi OFF</a><br></body></html>");
}

void enrollFP()
{
  bool flag = false;
  if (enroll_state == 0)
  {
    flag = FPSensor.UserNum() == true; // Must be compared with true (1)
    if (flag)
      webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a>" + String(l_ucFPID) + " IDs in total. Please put a new finger on.</a><br><a href=\"enroll\">Refresh or click here to continue.</a><br><a href=\"cancel\">Cancel</a><br></body></html>");
    else
      webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a>Please put a new finger on.</a><br><a href=\"enroll\">Refresh or click here to continue.</a><br><a href=\"cancel\">Cancel</a><br></body></html>");
    enrollTimer = millis();
    enroll_state++;
  }
  else // 1-6
  {
    if (enroll_state == 6)
      webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a>Stage " + String(enroll_state) + " (final), it was successful if the servo moved. Lift your finger if this happened.</a><br><a href=\"enroll\">Refresh or click here to continue.</a><br><a href=\"cancel\">Cancel</a><br></body></html>");
    else // 1-5
      webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a>Stage " + String(enroll_state) + ", continue to see whether it was successful.</a><br><a href=\"enroll\">Refresh or click here to continue.</a><br><a href=\"cancel\">Cancel</a><br></body></html>");
    if (enroll_state == 1)
    {
      flag = FPSensor.UserNum() == true;                            // Must be compared with true (1)
      flag = flag && l_ucFPID < 150;                                // ID value limit
      if (flag)                                                     // l_usFPID is valid
        flag = FPSensor.Enroll(l_ucFPID + 1, enroll_state) == true; // Must be compared with true (1)
      if (flag)
      {
        enrollTimer = millis();
        enroll_state++;
      }
    }
    else if (enroll_state == 6)
    {
      flag = FPSensor.Enroll(l_ucFPID + 1, enroll_state) == true; // Must be compared with true (1)
      if (flag)
      {
        enrollTimer = 0;
        enroll_state = 0;
      }
    }
    else // 2-5
    {
      flag = FPSensor.Enroll(l_ucFPID + 1, enroll_state) == true; // Must be compared with true (1)
      if (flag)
      {
        enrollTimer = millis();
        enroll_state++;
      }
    }
  }
}

void cancelEnroll()
{
  enrollTimer = 0;
  enroll_state = 0;
  webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a>Enroll process is now cancelled.</a><br><a href=\"home\">HOME</a><br></body></html>");
}

void confirmClear()
{
  webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a href=\"clear\">Confirm to clear ALL ID</a><br><a href=\"home\">HOME</a><br></body></html>");
}

void clearFP()
{
  if (FPSensor.Clear() == true) // Must be compared with true (1)
  {
    enrollTimer = 0;
    enroll_state = 0;
    webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a>ID storage is now clear!</a><br><a href=\"home\">HOME</a><br></body></html>");
  }
  else
    webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a>Fail to clear ID storage!</a><br><a href=\"home\">HOME</a><br></body></html>");
}

void showIDnum()
{
  bool flag = FPSensor.UserNum() == true; // Must be compared with true (1)
  if (flag)
    webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a>" + String(l_ucFPID) + " IDs in total.</a><br><a href=\"home\">HOME</a><br></body></html>");
  else
    webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a>Failure occurred.</a><br><a href=\"home\">HOME</a><br></body></html>");
}

void setHandler()
{
  byte timeout = 0;
  byte securityLevel = 0;
  byte allowRepeat = 0;
  if (webServer.hasArg("timeout"))
    timeout = FPSensor.SetTimeout(false, webServer.arg("timeout").toInt());
  else
    timeout = FPSensor.SetTimeout(true, 0);
  if (webServer.hasArg("security"))
    securityLevel = FPSensor.SecurityLevel(false, webServer.arg("security").toInt());
  else
    securityLevel = FPSensor.SecurityLevel(true, 5);
  if (webServer.hasArg("repeat"))
    allowRepeat = FPSensor.SetEnrollRepeat(false, webServer.arg("repeat").toInt());
  else
    allowRepeat = FPSensor.SetEnrollRepeat(true, false);

  String data = "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1>";
  if (timeout == 0)
    data += "<a>FPSensor timeout is set to disable or the reading is invalid.</a><br>";
  else
  {
    data += "<a>FPSensor timeout is set to ";
    data += String(timeout);
    data += ".</a><br>";
  }
  if (securityLevel == 0)
    data += "<a>Failed to read FPSensor security level.</a><br>";
  else
  {
    data += "<a>FPSensor security level is set to ";
    data += String(securityLevel - 1); // Cause valid return value is x+1
    data += ".</a><br>";
  }
  if (allowRepeat == 0)
    data += "<a>Failed to read whether repeated enrollment is allowed.</a><br>";
  else
  {
    data += "<a>Repeated enrollment status: ";
    data += String((allowRepeat - 1) == true); // Cause valid return value is x+1
    data += ".</a><br>";
  }
  data += "<a href=\"home\">HOME</a><br></body></html>";
  webServer.send(200, "text/html", data);
}

void gotoSleep()
{
  bool flag = FPSensor.Sleep() == true; // Must be compared with true (1)
  if (flag)
    webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a>FPSensor is now sleeping.</a><br><a href=\"home\">HOME</a><br></body></html>");
  else
    webServer.send(200, "text/html", "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1><a>Failure occurred.</a><br><a href=\"home\">HOME</a><br></body></html>");
}

void openTheDoor()
{
  if (webServer.hasArg("state"))
  {
    if (webServer.arg("state").toInt() != 0)
      HANDLE.write(cfg.Open_Angle); // Open the door without setting the timer
    else
      HANDLE.write(cfg.Close_Angle); // Close the door without resetting the timer
  }
  else
    HANDLE.write(cfg.Open_Angle); // Open the door without setting the timer
}

void setCfg()
{
  bool changed = false;
  byte Open_Angle = cfg.Open_Angle;                   // in degree 0-180, can be negative
  byte Close_Angle = cfg.Close_Angle;                 // in degree 0-180, can be negative
  unsigned long Open_Dur = cfg.Open_Dur;              // in ms
  unsigned long Enroll_Timeout = cfg.Enroll_Timeout;  // in ms
  unsigned int Wait_For_Finger = cfg.Wait_For_Finger; // in ms
  if (webServer.hasArg("open"))
  {
    changed = true;
    Open_Angle = webServer.arg("open").toInt();
    if (abs(Open_Angle) <= 180)
      cfg.Open_Angle = Open_Angle;
  }
  if (webServer.hasArg("close"))
  {
    changed = true;
    Close_Angle = webServer.arg("close").toInt();
    if (abs(Close_Angle) <= 180)
      cfg.Close_Angle = Close_Angle;
  }
  if (webServer.hasArg("dur"))
  {
    changed = true;
    Open_Dur = webServer.arg("dur").toInt();
    cfg.Open_Dur = Open_Dur;
  }
  if (webServer.hasArg("enroll"))
  {
    changed = true;
    Enroll_Timeout = webServer.arg("enroll").toInt();
    cfg.Enroll_Timeout = Enroll_Timeout;
  }
  if (webServer.hasArg("finger"))
  {
    changed = true;
    Wait_For_Finger = webServer.arg("finger").toInt();
    cfg.Wait_For_Finger = Wait_For_Finger;
  }

  String data = "<html><head><title>Control Panel</title></head><body><h1>Control Panel</h1>";
  if (changed)
  {
    bool saved = false;
    if (LittleFS.exists("/cfg.bin"))
      LittleFS.remove("/cfg.bin");
    File f = LittleFS.open("/cfg.bin", "w+");
    if (f)
    {
      f.write((unsigned char*)&cfg, sizeof(struct conf));
      if (f.size() == sizeof(struct conf))
      {
        f.seek(0, SeekSet);
        struct conf temp;
        f.read((unsigned char*)&temp, sizeof(struct conf));
        if (memcmp(&temp, &cfg, sizeof(struct conf)) == 0) // temp==cfg
          saved = true;
      }
      f.close();
    }
    if (saved)
      data += "<a>New config has been saved.</a><br>";
    else
      data += "<a>Error occured when saving new config.</a><br>";
  }
  data += "<a>Open_Angle: ";
  data += String(cfg.Open_Angle);
  data += " degree</a><br>";
  data += "<a>Close_Angle: ";
  data += String(cfg.Close_Angle);
  data += " degree</a><br>";
  data += "<a>Open_Dur: ";
  data += String(cfg.Open_Dur);
  data += " ms</a><br>";
  data += "<a>Enroll_Timeout: ";
  data += String(cfg.Enroll_Timeout);
  data += " ms</a><br>";
  data += "<a>Wait_For_Finger: ";
  data += String(cfg.Wait_For_Finger);
  data += " ms</a><br>";
  data += "<a href=\"home\">HOME</a><br></body></html>";
  webServer.send(200, "text/html", data);
}
