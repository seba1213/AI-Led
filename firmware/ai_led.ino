#include <TimeLib.h>
#include <NtpClientLib.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

#define NTP_TIMEOUT 1500
// а вдруг опять вернется
#define DAYLIGHT false
//дебаг
#define SERIAL_LOG true

// Параметры создаваемой сети, если предпологается использовать точкой, но будет работать только вкл. выкл. так как нет времени...
const char* ssid = "AI-Led"; // Enter SSID
const char* password = ""; // Enter password

boolean syncEventTriggered = false; // True if a time even has been triggered
NTPSyncEvent_t ntpEvent; // Last triggered event
byte relayMode, switchBtn, switchTimer; // Holds current relay state
byte resetCounter = 100; //перезапуск по счетчику
ESP8266WebServer server(80);  // Create an instance of the server on Port 80
DynamicJsonDocument eepromtimer(1498);
DynamicJsonDocument eepromsettings(540);
/*  
 *  Начальные настройки 
 */
void setup() {
  static WiFiEventHandler e2, e3;
  // Start Serial
  Serial.begin(115200);
  Serial.println ("");
  readEeprom();
  relayMode = 0;
  if (! eepromsettings.containsKey("loginA")) {
    resetSettings();
  }
  if (eepromsettings.containsKey("ssidP") && eepromsettings["ssidP"] != "") {
    WiFi.mode(WIFI_STA);
    if (eepromsettings["ipP"] != "" && eepromsettings["ipP"] != "" && eepromsettings["gatewayP"] != "" && eepromsettings["subnetP"] != ""  && eepromsettings["dnsP"] != "") {
      
      IPAddress ip(
        ipFromString(eepromsettings["ipP"])[0],
        ipFromString(eepromsettings["ipP"])[1],
        ipFromString(eepromsettings["ipP"])[2],
        ipFromString(eepromsettings["ipP"])[3]
      );
      IPAddress gateway(
        ipFromString(eepromsettings["gatewayP"])[0],
        ipFromString(eepromsettings["gatewayP"])[1],
        ipFromString(eepromsettings["gatewayP"])[2],
        ipFromString(eepromsettings["gatewayP"])[3]
      );
      IPAddress subnet(
        ipFromString(eepromsettings["subnetP"])[0],
        ipFromString(eepromsettings["subnetP"])[1],
        ipFromString(eepromsettings["subnetP"])[2],
        ipFromString(eepromsettings["subnetP"])[3]
      ); 
      IPAddress dns(
        ipFromString(eepromsettings["dnsP"])[0],
        ipFromString(eepromsettings["dnsP"])[1],
        ipFromString(eepromsettings["dnsP"])[2],
        ipFromString(eepromsettings["dnsP"])[3]
      );
      WiFi.config(ip, dns, gateway, subnet);
    }
    
    WiFi.begin(eepromsettings["ssidP"].as<String>(), eepromsettings["passworldP"].as<String>());
    NTP.onNTPSyncEvent ([](NTPSyncEvent_t event) {
        ntpEvent = event;
        syncEventTriggered = true;
    });
    e2 = WiFi.onStationModeDisconnected (onSTADisconnected);
    e3 = WiFi.onStationModeConnected (onSTAConnected);
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
  }
  //SERVER
  server.on("/timer-config/", hendlerTimerConfig);
  server.on("/style.css", Styles);
  server.on("/settings/", hendlerSettings);
  server.on("/", hendlerAuch);
  server.onNotFound(noPageFaund);
  // Start the server
  server.begin();
};
/* 
 * Manage network disconnection
 */
void onSTADisconnected(WiFiEventStationModeDisconnected event_info) {
  if (SERIAL_LOG) {
    Serial.printf ("Disconnected from SSID: %s\n", event_info.ssid.c_str ());
    Serial.printf ("Reason: %d\n", event_info.reason);
  }
  NTP.stop();
  resetCounter = resetCounter -1;
  Serial.printf ("Попыток до перезапуска: %d\n", resetCounter);
  if (resetCounter < 1) {
    resetFunc();
  }
  WiFi.reconnect();
};
/*
 * Удачное подключение
 */
void onSTAConnected(WiFiEventStationModeConnected ipInfo) {
  if (SERIAL_LOG) {
    Serial.printf ("Connected to %s\r\n", ipInfo.ssid.c_str ());
  }
  resetCounter = 100;
  const char* poolntp = "europe.pool.ntp.org";
  unsigned long updatentp = 10;
  long offsetntp = 3;
  if (eepromsettings.containsKey("poolNTP") && eepromsettings["poolNTP"] != "") {
    poolntp = eepromsettings["poolNTP"];
  }
  if (eepromsettings.containsKey("offsetNTP") && eepromsettings["poolNTP"] != "") {
    offsetntp = eepromsettings["offsetNTP"];  
  }
  if (eepromsettings.containsKey("updateNTP") && eepromsettings["updateNTP"] != "") {
    updatentp = eepromsettings["updateNTP"];
  }
  updatentp = updatentp * 60;
  NTP.setInterval(30, updatentp);
  NTP.setNTPTimeout(NTP_TIMEOUT);
  NTP.begin (poolntp, offsetntp, DAYLIGHT, 0);
};
/*
 * События обновления времени
 */
void processSyncEvent(NTPSyncEvent_t ntpEvent) {
    if (ntpEvent < 0) {
        Serial.printf ("Time Sync error: %d\n", ntpEvent);
        if (ntpEvent == noResponse)
            Serial.println ("NTP server not reachable");
        else if (ntpEvent == invalidAddress)
            Serial.println ("Invalid NTP server address");
        else if (ntpEvent == errorSending)
            Serial.println ("Error sending request");
        else if (ntpEvent == responseError)
            Serial.println ("NTP response error");
    } else {
        if (ntpEvent == timeSyncd) {
            Serial.print ("Got NTP time: ");
            Serial.println (NTP.getTimeDateString (NTP.getLastNTPSync ()));
        }
    }
}
/*
 * Слушатель порта
 */
void serialListener() {
  boolean recievedFlag = false;
  String strData = "";
  while (Serial.available() > 0) {  
    strData += (char)Serial.read();
    recievedFlag = true;
    delay(2);
  }
  if (recievedFlag) {
    if (strData.indexOf("reset") == 0) {
      resetSettings();
      Serial.println("Настройки сброшены");
      resetFunc();
    }
  }
};
/*
 * Ресет настроек
 */
void resetSettings() {
  if (SERIAL_LOG) {
    Serial.println("Старт сброса настроек");
  }
  DynamicJsonDocument defsettings(540);
  char *settings[] = {"dnsP", "subnetP", "gatewayP", "ipP", "passworldP", "ssidP", "passworldA", "loginA"};
  for (int i = 0; i < sizeof(settings)/sizeof(*settings); i++) {
    defsettings[settings[i]] = "";
  }
  defsettings["onS"] = "0xA0, 0x01, 0x01, 0xA2"; // Hex command to send to serial for open relay
  defsettings["offS"] = "0xA0, 0x01, 0x00, 0xA1"; // Hex command to send to serial for close relay
  defsettings["poolNTP"] = "europe.pool.ntp.org";
  defsettings["offsetNTP"] = 3; // 3 часа
  defsettings["updateNTP"] = 24*60; // 1 день
  String basesettings;
  serializeJson(defsettings, basesettings);
  eepromsettings = defsettings;
  writeStringEeprom(2000, basesettings);
  if (SERIAL_LOG) {
    Serial.println("Настройки сброшены успешно");
  }
}
/*
 * Крутим вертим...
 */
void loop() {
  if (syncEventTriggered) {
    if (SERIAL_LOG) {
      processSyncEvent(ntpEvent);
    }
    syncEventTriggered = false;
  }
  serialListener();
  CheckForChange();
  server.handleClient();
  delay(500);
};
/*
 * Обработчик индекс страницы
 */
void hendlerAuch() {
  if (!isLogin()) {
      server.requestAuthentication();
  } else {
      if (eepromsettings["loginA"] == "" || eepromsettings["ssidP"] == "") {
        server.sendHeader("Location", String("/settings/"), true);
      } else {
        server.sendHeader("Location", String("/timer-config/"), true);
      }
      server.send ( 302, "text/plain", "");
    }
};
/*
 * Полверка наличия авторизации или не заполненности настроек
 */
bool isLogin() {
  const char* loginA = eepromsettings["loginA"];
  const char* passworldA = eepromsettings["passworldA"];
  if (server.authenticate(loginA, passworldA) || eepromsettings["loginA"] == "") {
      return true;
  }
  return false;
};
/*
 * Обработчик страницы настроек
 */
void hendlerSettings() {
  if (SERIAL_LOG) {
    Serial.println("Старт хендлера страницы настроек");
  }
  if (!isLogin()) {
      server.requestAuthentication();
      if (SERIAL_LOG) {
        Serial.println("Хендлер завершен так-как не логин");
      }
      return;
  }
  DynamicJsonDocument serverparams = paransFronServerToJsoon();
  if (serverparams.containsKey("loginA")) {
    String settings;
    serializeJson(serverparams, settings);
    if (settings.length() < 539) {
      eepromsettings = serverparams;
      writeStringEeprom(2000, settings);
      resetFunc();
    } else {
      htmlHeader();
      server.sendContent("<p>Переданные данные слишком велики для сохранения</p>");
      server.sendContent("</div></body></html>");
      if (SERIAL_LOG) {
        Serial.println("Хендлер завершен так-как данные слишком велики для сохранения");
      }
      return;
    }
  }
  if (SERIAL_LOG) {
    Serial.println(ESP.getFreeHeap());
    Serial.println("Хендлер завершен далее html");
  }
  htmlHeader();
  HtmlSettingsForm();
};
/*
 * Сообщение если страница не найдена
 */
void noPageFaund() {
  server.send(404, "text/plain", "The requested URL was not found on this server.");
};
/*
 * Обработчик страницы с настройкой времени включения/выключения
 */
void hendlerTimerConfig() {
  if (SERIAL_LOG) {
    Serial.println("Старт хендлера страницы с настройкой времени включения/выключения");
  }
  if (!isLogin()) {
      server.requestAuthentication();
      if (SERIAL_LOG) {
        Serial.println("Хендлер завершен так-как не логин");
      }
      return;
  }
  DynamicJsonDocument serverparams = paransFronServerToJsoon();
  // react on parameters
  if (serverparams.containsKey("sCmd")) {
    // change the effect
    if (serverparams["sCmd"] == "ON") {
      switchBtn = 1;
    }
    else if (serverparams["sCmd"] == "OFF") {
      switchBtn = 0;
    }
    CheckForChange();
  } else if (serverparams.containsKey("t-1-on-hour")) {
    String timersettings;
    serializeJson(serverparams, timersettings); 
    if (timersettings.length() < 1498) {
      eepromtimer = serverparams;
      writeStringEeprom(1, timersettings);
    } else {
      htmlHeader();
      server.sendContent("<p>Переданные данные слишком велики для сохранения</p>");
      server.sendContent("</div></body></html>");
      if (SERIAL_LOG) {
        Serial.println("Хендлер завершен так-как данные слишком велики для сохранения");
      }
      return;
    }
  }
  if (SERIAL_LOG) {
    Serial.println(ESP.getFreeHeap());
    Serial.println("Хендлер завершен далее html");
  }
  // format the html response
  htmlHeader();
  htmlControlPage(); 
};
/*
 * Это должно делать софт ресет
 */
void resetFunc() {
  Serial.println("перезапуск...");
  delay(500);
  ESP.restart();
}
/*
 * Преобразование ip в массив
 */
int* ipFromString(String strIP) {
  if (SERIAL_LOG) {
    Serial.println("Старт преобразования ip в массив\n\r" + strIP);
  }
  int Parts[4] = {0,0,0,0};
  int Part = 0;
  for ( int i=0; i<strIP.length(); i++ )
  {
    char c = strIP[i];
    if ( c == '.' )
    {
      Part++;
      continue;
    }
    Parts[Part] *= 10;
    Parts[Part] += c - '0';
  }
  if (SERIAL_LOG) {
    Serial.println(ESP.getFreeHeap());
    Serial.println("Преобразование завершено");
  }
  return Parts;
};
/*
 * Преоборхование строки в hex
 */
byte (&convertStringToHex(String hexstring))[4] {
  byte hexarray[4] = {0x01, 0x01 , 0x01, 0x01};
  int iNextParam = 0, iPrevParam = 0;
  int i = 0;
  do {
      iNextParam = hexstring.indexOf(",", iPrevParam);
      String str = hexstring.substring(iPrevParam, iNextParam);
      hexarray[i] = strtol(str.c_str(),NULL,0);
      
      i++;
      iPrevParam = iNextParam + 1;
    } while (iNextParam > 0);
  
  return hexarray;
}
/*
 * Обработчик включения\выключения по таймеру
 */ 
void CheckForChange() {
  if (switchBtn == switchTimer) {
      switchBtn = 2;
      EEPROM.write(0, switchBtn);
      EEPROM.commit();
  }
  if (switchBtn == 2 && relayMode != switchTimer) {
    switch (switchTimer) {
      case 0: {
          Off();
          relayMode = 0;
        }
        break;
      case 1: {
          On();
          relayMode = 1;
        }
        break;
    }
  } else if (relayMode != switchBtn && switchBtn != 2) {
    switch (switchBtn) {
      case 0: {
          Off();
          relayMode = 0;
        }
        break;
      case 1: {
          On();
          relayMode = 1;
        }
        break;
    }
    EEPROM.write(0, switchBtn);
    EEPROM.commit();
  }
  if (timeStatus() != timeNotSet) {
    int currentday = weekday() - 1;
    if (currentday == 0) currentday = 7;
    if (int(eepromtimer["wday-" + String(currentday)]) == 1) {
      int currenttimemin = (hour() * 60) + minute();
      int settingstimeon = (int(eepromtimer["t-" + String(currentday) + "-on-hour"]) * 60) + int(eepromtimer["t-" + String(currentday) + "-on-min"]);
      int settingstimeof = (int(eepromtimer["t-" + String(currentday) + "-of-hour"]) * 60) + int(eepromtimer["t-" + String(currentday) + "-of-min"]);
      if(settingstimeon < settingstimeof) {
        if(currenttimemin >= settingstimeon && currenttimemin < settingstimeof ) {
          switchTimer = 1;
        } else if(currenttimemin >= settingstimeof) {
          switchTimer = 0;
        } else {
          switchTimer = 0;
        }
      }
      if (settingstimeon > settingstimeof){
        if(currenttimemin >= settingstimeon && currenttimemin <= 1439) {
          switchTimer = 1;
        } else if(currenttimemin < settingstimeof) {
          switchTimer = 1;
        } else if(currenttimemin >= settingstimeof && currenttimemin < settingstimeon) {
          switchTimer = 0;
        }
      }
    } else {
      switchTimer = 0;
    }
  }
};
/*
 * Преобразует аргументы в жсун
 * TODO Должен иметь срисок параметров для обработки
 */
DynamicJsonDocument paransFronServerToJsoon() {    
  DynamicJsonDocument serverparams(1498);
  if (server.args() > 0) {
    String paramname;
    for (int i = 0; i < server.args(); i++)  {
      paramname = server.argName(i);
      if (paramname == "plain" || paramname == "submit") continue;
      serverparams[paramname] = server.arg(paramname);
    }
  }
  return serverparams;
};
/*
 * Html шапка сайта
 */
void htmlHeader() {
  if (SERIAL_LOG) {
    Serial.println("Старт вывода html хедера");
  }
  char *weekdayarr[] = {0, "Понедельник","Вторник","Среда","Четверг","Пятница","Суббота","Воскресение"};
  int currentday = weekday() - 1;
  if (currentday == 0) currentday = 7;
  server.sendContent("<!DOCTYPE html>"
    "<html lang=\"ru\">"
    "<head>"
      "<meta charset=\"utf-8\">"
      "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\">"
      "<link rel=\"stylesheet\" type=\"text/css\" href=\"/style.css\">"
    "</head>");
  server.sendContent("<body>");
  server.sendContent("<div class=\"container\">"
    "<div class=\"header\">"
    "<div class=\"header-left\">"
      "<h1>AI-Led</h1>"
      "<a href=\"/timer-config/\">Настройка таймера</a></br>"
      "<a href=\"/settings/\">Настройки</a></br>"
    "</div>"
    "<div class=\"header-right\">");
    if (timeStatus() == timeNotSet) {
      server.sendContent("<p>Ограниченный функционал - время не определено</p>");
    } else {
      server.sendContent("<h1>" + NTP.getTimeStr() + "</h1><h3>" + String(weekdayarr[currentday]) + "</h3>"); 
    }
    server.sendContent("<h3>Работает:&nbsp;" + TimePrint() + "</h3>");
    server.sendContent("<h3>Состояние:&nbsp;");
    switch (relayMode) {
      case 0:
        server.sendContent("Выкл.</h3>");
        break;
      case 1:
        server.sendContent("Вкл.</h3>");
        break;
    }
  server.sendContent("</div></div>");
  if (SERIAL_LOG) {
    Serial.println(ESP.getFreeHeap());
    Serial.println("Конец вывода html хедера");
  }
};
/*
 * Html страница управления
 */
void htmlControlPage() {
  if (SERIAL_LOG) {
    Serial.println("Старт вывода html страница управления");
  }
  int days = 7;
  String sHour, sMin;
  char *weekdayarr[] = {0, "Понедельник","Вторник","Среда","Четверг","Пятница","Суббота","Воскресение"};
  ///// Buttons
  server.sendContent("<div class=\"btnonoff\"><form method=\"POST\" action=\"/timer-config/\">");
  server.sendContent("<button name=\"sCmd\" value=\"ON\" onclick=\'this.form.submit();'>Включить</button>");
  server.sendContent("<button name=\"sCmd\" value=\"OFF\" onclick=\'this.form.submit();'>Выключить</button>");
  server.sendContent("</form></div>");
  server.sendContent("<form method=\"POST\" action=\"/timer-config/\">");
  for (int i = 1; i <= days; i++) {
    String checked;
    if (int(eepromtimer["wday-" + String(i)]) == 1) checked="checked ";
    server.sendContent("<div class=\"day\"><div class=\"day-left\">"
      "<input " + checked + "type=\"checkbox\" name=\"wday-" + String(i) + "\" value=\"1\"><label  for=\"wday-" + String(i) + "\">" + String(weekdayarr[i]) + "</label></div>");
    server.sendContent("<div class=\"day-right\"><div class=\"time-on\"><span>Время Вкл. </span>");
    server.sendContent("<select name=\"t-" + String(i) + "-on-hour\" id=\"t-" + String(i) + "-on-hour\">");
    for(int j = 0; j < 24; j++) {
      String selected;
      if (int(eepromtimer["t-" + String(i) + "-on-hour"]) == j) selected = "selected ";
      server.sendContent("<option " + selected + "value=\"" + String(j) + "\">" + String(j) + "</option>");
    }
    server.sendContent("</select><label for=\"t-" + String(i) + "-on-hour\">Часов</label>");
    server.sendContent("<select name=\"t-" + String(i) + "-on-min\" id=\"t-" + String(i) + "-on-min\">");
    for(int j = 0; j < 60; j++) {
      String selected;
      if (int(eepromtimer["t-" + String(i) + "-on-min"]) == j) selected = "selected ";
      server.sendContent("<option " + selected + "value=\"" + String(j) + "\">" + String(j) + "</option>");
    }
    server.sendContent("</select><label for=\"t-" + String(i) + "-on-min\">Минут</label></div><div class=\"time-of\"><span>Время Выкл.</span>");
    server.sendContent("<select name=\"t-" + String(i) + "-of-hour\" id=\"t-" + String(i) + "-of-hour\">");
    for(int j = 0; j < 24; j++) {
      String selected;
      if (int(eepromtimer["t-" + String(i) + "-of-hour"]) == j) selected = "selected ";
      server.sendContent("<option " + selected + "value=\"" + String(j) + "\">" + String(j) + "</option>");
    }
    server.sendContent("</select><label for=\"t-" + String(i) + "-of-hour\">Часов</label>");
    server.sendContent("<select name=\"t-" + String(i) + "-of-min\" id=\"t-" + String(i) + "-of-min\">");
    for(int j = 0; j < 60; j++) {
      String selected;
      if (int(eepromtimer["t-" + String(i) + "-of-min"]) == j) selected = "selected ";
      server.sendContent("<option " + selected + "value=\"" + String(j) + "\">" + String(j) + "</option>");
    }
    server.sendContent("</select><label for=\"t-" + String(i) + "-of-min\">Минут</label></div></div></div>");
  }    
  server.sendContent("<input type=\"submit\" value=\"Сохранить\"></form>");
  server.sendContent("</div></body></html>");
  if (SERIAL_LOG) {
    Serial.println(ESP.getFreeHeap());
    Serial.println("Конец вывода html страница управления");
  }
};
/*
 * Выключить
 */
void Off() {
  Serial.write(convertStringToHex(eepromsettings["offS"]), sizeof(convertStringToHex(eepromsettings["offS"])));
};
/*
 * Включить
 */
void On() {
  Serial.write(convertStringToHex(eepromsettings["onS"]), sizeof(convertStringToHex(eepromsettings["onS"])));
};
/*
 * Время жизни
 */
String TimePrint(){
  String lifetime;
  int time=millis()/1000;
  if (time/60/60<10) { lifetime += "0"; }
  lifetime += time/60/60;
  lifetime +=  ":";
  if (time/60%60<10) { lifetime += "0"; }
  lifetime += (time/60)%60;
  lifetime += ":";
  if (time%60<10) { lifetime += "0"; }
  lifetime += time%60;
  return lifetime;
};
/*
 * Стили
 */
void Styles() {
  if (SERIAL_LOG) {
    Serial.println("Старт вывода стилей");
  }
  server.sendContent(
    ".container {width: 50%; margin: auto;background-color: #D3D3D3;padding: 10px;min-width: 520px;}"
    "body{background-color: #6E6E6E;margin: 0;color:#323232}"
    ".header {display: flex;flex-wrap: wrap;}"
    ".header-left {flex: 1.3;}"
    ".header-right {flex: 0.7;}"
    ".btnonoff button[value=\"OFF\"] {margin-left: 30px;background-color: #E8502D;}"
    ".btnonoff {margin-bottom: 20px;}"
  );
  server.sendContent(
    "form[action=\"/settings/\"] li {list-style: none;}"
    "form[action=\"/settings/\"] li>div {display: inline;}"
    "#saveForm {margin: 30px auto 20px auto;width: 200px;display: block;}"
    "form[action=\"/settings/\"] li>label {min-width: 110px;display: inline-block; margin-bottom: 15px;}"
  );
  server.sendContent(
    "form[action=\"/timer-config/\"] .day {display: flex; margin-bottom: 10px;border-bottom: 2px solid #828282;padding-bottom: 10px;}"
    "form[action=\"/timer-config/\"] .day-left {padding: 10px;min-width: 150px;display: flex;align-items: center;}"
    "form[action=\"/timer-config/\"] .time-on {margin-bottom: 5px;}"
    "form[action=\"/timer-config/\"] select {min-width: 60px;margin: 0 5px 0 10px;}"
    "form[action=\"/timer-config/\"]>input {margin: 30px auto 20px auto;width: 150px;display: block;}"
 );
 server.sendContent(
    "input[type=\"text\"], select {font-size: 13px;padding: 6px 0 4px 10px;border: 1px solid #cecece;background: #F6F6f6;border-radius: 4px;}"
    "input[type=\"submit\"], button {display: inline-block;line-height: 20px;padding: 10px 20px;background-color: #017EB8;color: #F6F6f6;text-align: center;z-index: 2;border-radius: 4px;border: none;}"
  );
  server.sendContent(
    "input[type=\"checkbox\"] {width: 40px;height: 20px;-webkit-appearance: none;-moz-appearance: none;background: #c6c6c6;outline: none;border-radius: 50px;box-shadow: inset 0 0 5px rgba(0,0,0, .2);transition: 0.5s;position: relative;}"
    "input:checked[type=\"checkbox\"] {background: #017EB8;}"
    "input[type=\"checkbox\"]::before {content: '';position: absolute;width: 20px;height: 20px;border-radius: 50%;top: 0;left: 0;background: #F6F6f6;transform: scale(1.1);box-shadow: 0 2px 5px rgba(0,0,0, .2);transition: 0.5s;}"
    "input:checked[type=\"checkbox\"]::before {left: 20px;}"
  );
  if (SERIAL_LOG) {
    Serial.println(ESP.getFreeHeap());
    Serial.println("Конец вывода html стилей");
  }
};
/*
 * Запись строки в еепром
 */
void writeStringEeprom(int startbyte,String data) {
  if (SERIAL_LOG) {
    Serial.println("Запись строки в еепром\n\r" + data);
  }
  int _size = data.length();
  int i;
  for(i=0;i<_size;i++)
  {
    EEPROM.write(startbyte+i,data[i]);
  }
  EEPROM.write(startbyte+_size,'\0');
  EEPROM.commit();
  if (SERIAL_LOG) {
    Serial.println(ESP.getFreeHeap());
    Serial.println("Записано");
  }
}; 
/* 
 *  Чтение и декодирование данных из еепром
 *  TODO сделать эту функцию нормально
 */
void readEeprom() {
  if (SERIAL_LOG) {
    Serial.println("Чтение и декодирование данных из еепром");
  }
  EEPROM.begin(4000);
  switchBtn = EEPROM.read(0);
  char data[1498], data2[540];
  int len=0;
  unsigned char k;
  int startbyte = 1;
  k=EEPROM.read(startbyte);
  while(k != '\0' && len < 1497)
  {    
    k=EEPROM.read(startbyte + len);
    data[len]=k;
    len++;
  }
  data[len]='\0';
  deserializeJson(eepromtimer, String(data));

  len=0;
  startbyte = 2000;
  k=EEPROM.read(startbyte);
  while(k != '\0' && len < 539)
  {    
    k=EEPROM.read(startbyte + len);
    data2[len]=k;
    len++;
  }
  data2[len]='\0';
  deserializeJson(eepromsettings, String(data2));
  if (SERIAL_LOG) {
    Serial.println(ESP.getFreeHeap());
    Serial.println("Завершено получена строка \n\r" + String(data2));
  }
};
/*
 * Html форма настроек
 */
void HtmlSettingsForm() {
  if (SERIAL_LOG) {
    Serial.println("Старт вывода html страницы настроек");
  }
  server.sendContent("<form method=\"POST\" action=\"/settings/\"><ul >");
    server.sendContent("<li class=\"section_break\"><h3>Авторизация</h3></li>");
    server.sendContent("<li><label class=\"description\" for=\"loginA\">Логин </label><div>"
      "<input name=\"loginA\" class=\"element\" type=\"text\" value=\"" + eepromsettings["loginA"].as<String>() + "\"/> </div></li>");
    server.sendContent("<li><label class=\"description\" for=\"passworldA\">Пароль </label><div>"
      "<input name=\"passworldA\" class=\"element\" type=\"text\" value=\"" + eepromsettings["passworldA"].as<String>() + "\"/></div></li>");
    server.sendContent("<li class=\"section_break\"><h3>Подключение к точке доступа</h3></li>");
    server.sendContent("<li><label class=\"description\" for=\"ssidP\">ssid </label><div>"
      "<input name=\"ssidP\" class=\"element\" type=\"text\" value=\"" + eepromsettings["ssidP"].as<String>() + "\"/></div></li>");
    server.sendContent("<li><label class=\"description\" for=\"passworldP\">Пароль </label><div>"
      "<input name=\"passworldP\" class=\"element\" type=\"text\" value=\"" + eepromsettings["passworldP"].as<String>() + "\"/></div></li>");
    server.sendContent("<li><label class=\"description\" for=\"ipP\">Ip адрес </label><div>"
      "<input name=\"ipP\" class=\"element\" type=\"text\" value=\"" + eepromsettings["ipP"].as<String>() + "\"/></div></li>");
    server.sendContent("<li><label class=\"description\" for=\"gatewayP\">Gateway </label><div>"
    "<input name=\"gatewayP\" class=\"element\" type=\"text\" value=\"" + eepromsettings["gatewayP"].as<String>() + "\"/></div></li>");
    server.sendContent("<li><label class=\"description\" for=\"subnetP\">Маска подсети </label><div>"
    "<input name=\"subnetP\" class=\"element\" type=\"text\" value=\"" + eepromsettings["subnetP"].as<String>() + "\"/> </div></li>");
    server.sendContent("<li><label class=\"description\" for=\"dnsP\">DNS </label><div>"
    "<input name=\"dnsP\" class=\"element\" type=\"text\" value=\"" + eepromsettings["dnsP"].as<String>() + "\"/> </div></li>");
    server.sendContent("<li class=\"section_break\"><h3>Сигнал управления</h3></li>");
    server.sendContent("<li><label class=\"description\" for=\"onS\">Сигнал Вкл. </label><div>"
      "<input name=\"onS\" class=\"element\" type=\"text\" value=\"" + eepromsettings["onS"].as<String>() + "\"/></div></li>");
    server.sendContent("<li><label class=\"description\" for=\"offS\">Сигнал Выкл. </label><div>"
    "<input name=\"offS\" class=\"element\" type=\"text\" value=\"" + eepromsettings["offS"].as<String>() + "\"/></div></li>");
    server.sendContent("<li class=\"section_break\"><h3>Обновления времени</h3></li>");
    server.sendContent("<li><label class=\"description\" for=\"poolNTP\">Сервер </label><div>"
      "<input name=\"poolNTP\" class=\"element\" type=\"text\" value=\"" + eepromsettings["poolNTP"].as<String>() + "\"/></div></li>");
    server.sendContent("<li><label class=\"description\" for=\"offsetNTP\">UTC(h)</label><div>"
      "<input name=\"offsetNTP\" class=\"element\" type=\"text\" value=\"" + eepromsettings["offsetNTP"].as<String>() + "\"/></div></li>");
    server.sendContent("<li><label class=\"description\" for=\"updateNTP\">Обновление(min) </label><div>"
      "<input name=\"updateNTP\" class=\"element\" type=\"text\" value=\"" + eepromsettings["updateNTP"].as<String>() + "\"/></div></li>");
    
    server.sendContent("<li class=\"buttons\"><input id=\"saveForm\" class=\"button_text\" type=\"submit\" name=\"submit\" value=\"Сохранить и перезагрузить\" /></li>");
  server.sendContent("</ul></form>");
  server.sendContent("</div></body></html>");
  if (SERIAL_LOG) {
    Serial.println(ESP.getFreeHeap());
    Serial.println("Конец вывода html страницы настроек");
  }
};
