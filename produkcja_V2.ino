#include <time.h>
#include <math.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>
#include <SPI.h>
#include <SD.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "index_html.h"

const char* ssid = "KUBEK_0";  /// do zmiany na każde ESP
String pageTitle = "KUBEK Testy"; /// do zmiany na każde ESP
AsyncWebServer server(80);



FASTLED_USING_NAMESPACE

#define DATA_PIN 2
#define NUM_LEDS 35
#define MAX_POWER_MILLIAMPS 2200
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

/////// świecenie tryby

///baza modów świecenia
enum Mode { PACIFICA,
            RAINBOW,
            CYLON,
            LAVA,
            TWINKLE,
            PULSEB,
            PULSEG,
            PULSER,
            FIRE,
            COLORWAVES,
            BUBBLES,
            SPEED,
            TILT };
int trybow=12; //aktualna ilość trybów (od 0) 
Mode currentMode = PACIFICA;  //domyślny bo go lubię i chuj
Mode overrideMode = PACIFICA;
bool overrideActive = false;

const char* modeNames[] = {
  "Pacifica", "Rainbow", "Cylon", "Lava", "Twinkle",
  "Pulse Blue", "Pulse Green", "Pulse Red", "Fire", "Colorwaves",
  "Bubbles"};
// unsigned long transitionStart = 0;
// const unsigned long transitionDuration = 500; // ms
// bool inTransition = false;
// Mode previousMode;

// CRGB ledsPrev[NUM_LEDS];

int T_sygnalizacji = 60000;  ///czas bazowy sygnalizacji
int DELAYVAL = 2000;         //czas bazowy wyświetlania efektów systemowych [ms]
int T_zmiany = 30000;       ///czas bazowy przejść między trybami zwykłymi
unsigned long teraz=0; ///czas pętli
unsigned long lastSignalTime = 0;
unsigned long lastEffectChange = 0;
unsigned long blinkStartTime = 0;
bool isBlinking = false;

unsigned long gyroEffectStart = 0;
bool gyroEffectActive = false;

unsigned long discoStartTime = 0;
bool discoActive = false;

///do konkretnych trybów świecenia
int cylonPos = 0;
int cylonDir = 1;
uint8_t rainbowHue = 0;
uint16_t pacificaWave = 0;
uint8_t pulseBrightness = 0;
bool pulseUp = true;

float BRIGHTNESS_MAX = 250 * 0.5;  //peak jasność

int brightness_Gyro, green, blue, red;

/////INICJALIZACJA MOFUŁÓW
Adafruit_MPU6050 mpu;
Adafruit_INA219 ina219;

//////do obsługi żyroskopu
float GyroX = 0;  /// kąt nachylenia osi X
float GyroY = 0;  /// kąt nachylenia osi Y
float GyroZ = 0;  /// kąt nachylenia osi Z
float AccXold = 0;
float AccYold = 0;
float AccZold = 9.8;

float Gyro_mix_ = 0;  //
float Acc_mix_ = 0;
float Gyro_mix_OLD = 0, Acc_mix_OLD = 0;
float AccX = 0;
float AccY = 0;
float AccZ = 0;
float Acc_treshhold = 3.4;  // wartość przyspieszenia triggerująca efekty
float Acc_T = 1000;          //czas działania efektów od przyspieszeń
float Gyro_treshhold = 15;  //no idea, jeśli któraś z osi przekroczy ten próg kubek zacznie świecić póki kąty się nie wyzerują
double pitchFromAccel = 0;
double rollFromAccel = 0;
double _pitch = 0;
double _roll = 0;
long pitch0 = 0;
long roll0 = 0;
double Acc_mix, Gyro_mix;
float GyroEffectDuration = 30 * 1000;  //czas trwania efektu od kątu pochylenia
float odchylZgraniczny = 8;            ///odchył od pionu do uwzględniania przechyłu w przyspieszeniach


///////////ogólne i sprzętowe
#define laduj 0                     //enable ładowania ogniw
#define wlancz A0                   // pozycja włącznika przelicznik 2,09 (4V->1,9V)
volatile bool changeState = false;  //do przerwania obsługującego wyłączenie ładowania
float przelancznik = 4;             ///napiecie miedzy wlacznikiem i stepUP'em
int pstryczek = 0;                  // stan włącznika; 0==wyłączony; 1= włączone
int ladowanie = 1;                  // 0 == nie ładuj; 1== żądanie ładowania
float termika = 0;                  // czy jest bezpiecznie temicznie, 0==tak, >1 za ciepło
float temp1 = 20;                   //temp z ntc
                       

////////////ina219
float shuntvoltage = 0;
float busvoltage = 0;
float current_mA = 0;
float loadvoltage = 0;
float power_mW = 0;

//////////////stan baterii
float SOC;                 // poziom naładowania %; 2,5V==0; 4,2V==100%
double mAh;                // poziom naładowania w mAh
float mAh1 = 0, mAh2 = 0;  //pomocnicze
unsigned long previousMillis = 0;
unsigned long czas = 0;  //czas między petlami
unsigned long currentMillis = 0;
long czas_ladowania = 0;
long czas_zasilZEW = 0;
int naladowane = 0;

// D5, D6, D7, D8 // SCK, MISO, MOSI, CS
#define SD_CS_PIN D8

File dataFile;
int obrot = 0;  //ilość pętli do zapisu mAh

////////31 wartości do identyfikowania *początkowego* SOC
float napiecia[] = { 4.2, 4.06, 4.03, 3.99, 3.97, 3.94, 3.93, 3.91, 3.90, 3.89, 3.88, 3.8799, 3.8796, 3.873, 3.871, 3.870, 3.868, 3.867, 3.866, 3.865, 3.864, 3.862, 3.8638, 3.79, 3.43, 3.38, 3.14, 3.03, 2.937, 2.7, 2.4 };                                                               //progi napięc w V
float mAh_OCV[] = { 0, 0.001, 21.54, 78.01, 152.30, 229.56, 321.693, 452.45, 586.18, 722.88, 868.49, 1026.02, 1141.91, 1284.54, 1415.36, 1543.09, 1685.73, 1795.69, 1899.782, 2015.67, 2122.58, 2214.71, 2312.77, 2384.11, 2443.53, 2485.14, 2523.74, 2547.54, 2556.463, 2571.322, 2575 };  //progi pojemności mAh
int mAh_max = 2575;                                                                                                                                                                                                                                                                         //max SOC - zmierzyć kiedyś

///////////////////////////wifi
// unsigned long currentUnixTime = 0;
unsigned long diff = 0;
time_t deviceTime = 0;  // Czas ustawiany przez przeglądarkę
time_t SDTime = 0;  // Czas odczytany z SD
// ====== FUNKCJA DO PODSTAWIENIA TYTUŁU ======
String processor(const String& var) {
  if (var == "TITLE") return pageTitle;
  return String();
}



/////////////////////////////////
void setup() {

pinMode(laduj, OUTPUT);  ///okazuję się że w esp trzeba ustawić tryb pinu... ciekawe ile rzeczy mi przez to nie działało
digitalWrite(laduj, LOW);
Serial.begin(9600);

/////////////////wifi
 WiFi.softAP(ssid);
// Strona główna
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);

  });


/////pobierz aktualny stan z ESP
server.on("/getStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
  String json = "{";
  json += "\"temp\":" + String(round(temp1)) + ",";
  json += "\"soc\":" + String(round(SOC)) + ",";
  json += "\"mode\":" + String(currentMode) + ",";
  json += "\"modeName\":\"" + String(modeNames[currentMode]) + "\"";
  json += "}";
  request->send(200, "application/json", json);
});


  // Ustaw tryb działania
  server.on("/setMode", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("mode")) {
  int modeInt = request->getParam("mode")->value().toInt();
  if (modeInt >= 0 && modeInt <= trybow-2) {  // walidacja zakresu trybów (bez SPEED i TILT)
    currentMode = static_cast<Mode>(modeInt);
    request->send(200, "text/plain", "OK");
    lastEffectChange =millis();
  } else {
    request->send(400, "text/plain", "Invalid mode value");
  }
} else {
  request->send(400, "text/plain", "Missing mode param");
}
});




  // Ustaw czas (unix)
  server.on("/setTime", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("unix")) {
      deviceTime = request->getParam("unix")->value().toInt();
      request->send(200, "text/plain", "Time set");
    } else {
      request->send(400, "text/plain", "Missing unix param");
    }
  });

  // Odpowiedź z aktualnym trybem
server.on("/getMode", HTTP_GET, [](AsyncWebServerRequest *request) {
  request->send(200, "text/plain", String(currentMode));
});

  // Start serwera
  server.begin();
Serial.println("HTTP server started");
 /////

  

  // pixels.begin(); //start LED
  delay(2000);  // 3 second delay for boot recovery, and a moment of silence
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
    .setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_POWER_MILLIAMPS);
  FastLED.clear();
  FastLED.show();



  ////////ina219 miernik prądu
  ina219.begin();
  delay(500);

  /////karta SD i początkowe SOC na bazie zapisu z SD
if (!SD.begin(SD_CS_PIN)) {
    Serial.println("Błąd inicjalizacji karty SD!");
    return;
  }


if (SD.exists("/pojemnosc")) {
  dataFile = SD.open("pojemnosc", FILE_READ);
  if (dataFile) {
    String lastLine;
    while (dataFile.available()) {
      lastLine = dataFile.readStringUntil('\n');
    }
    dataFile.close();

    int sep1 = lastLine.indexOf(';');
    int sep2 = lastLine.indexOf(';', sep1 + 1);
      if (sep1 > 0 && sep2 > sep1) {
      SDTime = lastLine.substring(0, sep1).toInt();
      mAh1 = lastLine.substring(sep1 + 1, sep2).toFloat();
      }

  }
} else 
{
  // Brak pliku – zapis inicjalizacyjny
  dataFile = SD.open("pojemnosc", FILE_WRITE);
  if (dataFile) {
    mAh1 = mAh_max;
    dataFile.print(deviceTime);
    dataFile.print(";");
    dataFile.print(mAh1);
    dataFile.print(";");
    dataFile.print(current_mA);
    dataFile.print(";");
    dataFile.print(temp1);
    dataFile.print(";");
    dataFile.println(loadvoltage);
    dataFile.close();
  }
}
if (deviceTime > 0 && SDTime > 0) {
        unsigned long diff = (deviceTime - SDTime) / 86400;
        Serial.print("Ostatni zapis był ");
        Serial.print(diff);
        Serial.println(" dni temu.");
      }
    // delay(100);
    // Serial.print("Czas rzeczywisty: ");
    // Serial.println(deviceTime);
    // Serial.print("Odczytany czas: ");
    // Serial.println(SDTime);
    // Serial.print("Odczytane mAh: ");
    // Serial.println(mAh1);
  //////////////////początkowy SOC z napięcia
  digitalWrite(laduj, LOW);
  delay(500);
  busvoltage = ina219.getBusVoltage_V();
  Serial.print("odczytane napiecie pierwotnie ");
  Serial.println(busvoltage);

  float mAh2 = findClosestmAh(busvoltage, napiecia, mAh_OCV, 30);

if (diff>0){
  mAh1 = mAh1*(1 - (0.15*diff/30));             ///odejmujemy za postój (średnio 15% per miesiąc), kiedyś do poprawy na bazie modelu baterii
  mAh = ( (mAh1*5)+ (mAh2)) / 6;  ///szacowanie SOC jak mam wartościową historię
}
else
{ mAh = ( (mAh1*1)+ (mAh2*3)) / 4;  ///szacowanie SOC dla biednych jak nie wiem co się działo wcześniej
}

  SOC = (mAh / mAh_max) * 100;      ///SOC pierwotne



  Serial.print("mAh1 ");
  Serial.println(mAh1);
  Serial.print("mAh2 ");
  Serial.println(mAh2);
  Serial.print("obliczone mAh ");
  Serial.println(mAh);


  Wire.begin();  ///nie wiem czy to musi tu być
  /////////żyroskop konfig
  mpu.begin();


  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);  //arbitralnie

  mpu.setGyroRange(MPU6050_RANGE_500_DEG);  //arbitralnie

  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);  //chyba częściej nie ma sensu


  
  ////
  previousMillis = millis();
}
///////////////////////////////////////////

///////////znajduje napięcie mAh po pierwotnym napięciu z INA219
float findClosestmAh(float busvoltage, float napiecia[], float mAh_OCV[], int size) {
  float closest_mAh = mAh_OCV[0] - 100;
  float min_diff = fabs(busvoltage - (napiecia[0] * 1000));

  for (int i = 1; i < size; i++) {
    float diff = fabs(busvoltage - (napiecia[i] * 1000));
    if (diff < min_diff) {
      min_diff = diff;
      closest_mAh = mAh_OCV[i];
    }
  }

  return closest_mAh;
}


////////// z fastled
ICACHE_RAM_ATTR void handleInterrupt() {
  changeState = true;
}

void loop() {
// server.handleClient();

  ///czas
  teraz = millis();  //czas od rozruchu
  czas = teraz - previousMillis;  //czas pętli
  previousMillis = teraz;
  time_t currentTime = deviceTime + (millis() / 1000);
  // Serial.print("czas ");
  // Serial.println(czas);

  /////////INA219
  pomiary_ele();
  obl_SOC();

  //////////ładowanie


  //kolejność wykonywania ważna, najpierw żądania dbające os SOH ogniw, bezpieczeństwo termiczne tuż przed ładowaniem
  tryb_muzeum();  //odpowiada za nie ładowanie permanente przy dłuższym postoju na zasilaniu zew.
  term_bezp();
  tryb_ladowania();  // odpowiada za ładowanie

  ///////żyroskop
  gyro();

  /////wybierz tryb świecenia na podstawie akcji

  if (mAh < 0.1 * mAh_max && teraz - lastSignalTime >= T_sygnalizacji) {
    SOC10();
    lastSignalTime = teraz;
  } else if (mAh < 0.05 * mAh_max && teraz - lastSignalTime >= T_sygnalizacji) {
    SOC5();
    lastSignalTime = teraz;
  } else if (mAh > 0.99 * mAh_max && teraz - lastSignalTime >= T_sygnalizacji && naladowane == 1) {
    SOC100();
    lastSignalTime = teraz;
  }

  // Normalne efekty
  else if (!discoActive && !gyroEffectActive) {
    if (teraz - lastEffectChange > T_zmiany) {
      currentMode = static_cast<Mode>((currentMode + 1) % (trybow+1-2));  // 7 domyślnych trybów na 05.04.2025, 11 na 01.08.2025
      lastEffectChange = teraz;
    }
  }

  // Wyzwolenie trybu dyskoteki
  if (!discoActive && Acc_mix > Acc_treshhold) {
    discoActive = true;
    discoStartTime = teraz;
    currentMode = SPEED;
    // Serial.print("Disco");
    // Serial.println("");
    
  }
  
  // Wyłączenie efektów specjalnych po czasie
  if (discoActive && teraz - discoStartTime > Acc_T) {
    discoActive = false;
    currentMode = static_cast<Mode>(random(0, trybow-2));
    lastEffectChange = teraz;
    gyroEffectActive = false;
  }

// Wyzwolenie trybu koloru z żyroskopu
  if (!discoActive && !gyroEffectActive && Gyro_mix > Gyro_treshhold) {
    gyroEffectActive = true;
    gyroEffectStart = teraz;
    currentMode = TILT;
    // Serial.print("FullTilt ");
    // Serial.println("");
  }
  else if(gyroEffectActive && Gyro_mix < Gyro_treshhold) //jak już płasko to wyłącz
  {
    gyroEffectActive = false;
    currentMode = static_cast<Mode>(random(0, trybow-2));
    lastEffectChange = teraz;
  }
  else if (gyroEffectActive && teraz - gyroEffectStart > GyroEffectDuration) { //jak trwa to za długo to wyłącz
    gyroEffectActive = false;
    currentMode = static_cast<Mode>(random(0, trybow-2));
    lastEffectChange = teraz;
  }

////"płynne" przejścia
// if (inTransition) {
//   transitionUpdate();
// }
// else 
// {

  switch (currentMode) 
  { //ostatnie dwa tryby są aktywowane przez żyroskop

    case PACIFICA:  //0 niebieski pływający kolorek
      EVERY_N_MILLISECONDS(20) {
         pacifica_loop();
      }
      break;
    case RAINBOW: //1 tęcza powoli płynąca
      EVERY_N_MILLISECONDS(10) {
        rainbowEffect();
      }
      break;
    case CYLON:  //2 wędrujący czerowny, zielony i niebieski pixel
      EVERY_N_MILLISECONDS(80) {
        cylonEffect();
      }
      break;
    case LAVA: //3 pulsująca czerwień
      EVERY_N_MILLISECONDS(50) {
        lavaLamp();
      }
      break;
    case TWINKLE: //4 biały pulsuje i znika
      EVERY_N_MILLISECONDS(50) {
        twinkleFOX();
      }
      break;
    case PULSEB: //5 niebieski pulsuje i znika
      EVERY_N_MILLISECONDS(30) {
        pulseFadeB();
      }
      break;
    case PULSEG: //6 zielony pulsuje i znika
      EVERY_N_MILLISECONDS(30) {
        pulseFadeG();
      }
      break;
        case PULSER: //7 czerwony pulsuje i znika
      EVERY_N_MILLISECONDS(30) {
        pulseFadeR();
      }
      break;  
    case FIRE: //8 pomarańcz pływający
      EVERY_N_MILLISECONDS(300) {
        fireEffect();
      }
      break;
    case COLORWAVES: //9 krążąca szybko tęcza
      EVERY_N_MILLISECONDS(30) {
        colorWaves();
      }
      break;
    case BUBBLES: //10 bąbelki krążące
      EVERY_N_MILLISECONDS(50) {
        bubbleEffect();
      }
      break;
    case SPEED: //akcelerometr
       EVERY_N_MILLISECONDS(50)
      {
        DISCO();
      }
      break;
    case TILT: //żyroskop
      EVERY_N_MILLISECONDS(50)
      {
        FullTilt();
      }
      break;
  }
// }
  Serial.println("");
  Serial.println("************");
  // Serial.print("roll ");
  // Serial.println(roll);
  //   Serial.print("pitch ");
  // Serial.println(pitch);
  // Serial.print("zmiana przyspieszeń ");
  // Serial.println(Acc_mix);
  Serial.print("tryb świecenia ");
  Serial.println(currentMode);
  // Serial.print("Gyro effect  ");
  // Serial.println(gyroEffectActive);
  // Serial.print("Acc effect  ");
  // Serial.println(discoActive);
  // Serial.print("teraz  ");
  // Serial.println(teraz);
  // Serial.println("****");
  // Serial.println("");
}

////////////////////////////////
void tryb_ladowania()  ///laduj==1 żądanie ładowania; ładuj == 0 nie ładujemy
{
  przelancznik = analogRead(wlancz) * 3.3 / 1024 * 2.1;  ///odczyt napiecia //*2.1<- dzielnik//1024==3,3V
  if (przelancznik > -1 && przelancznik < 1) {
    pstryczek = 0;
    // if (mAh<mAh_max)
    // {ladowanie=1;}
  } else {
    pstryczek = 1;
    digitalWrite(laduj, LOW);
  }

  // Serial.print("napiecie odczytane na przelanczniku ");
  // Serial.println(przelancznik);


  if (termika == 0)  ///jak termicznie bezpiecznie, dopiero myśl o ładowaniu
  {
    if (pstryczek == 0)  ///jak wyłącznik jest w pozycji off, to dopiero wolno ładować (inaczej jebie zwarciem)
    {
      if (ladowanie == 1)  //żądanie ładowania =1; nie ładuj=0
      {
        digitalWrite(laduj, HIGH);
      } else {
        digitalWrite(laduj, LOW);
      }
    } else {
      digitalWrite(laduj, LOW);
      // ladowanie=0;

      // Serial.print("flaga ładowania ");
      // Serial.println(ladowanie);
    }

    // Serial.println("");
    // Serial.print("temperatura ");
    // Serial.println(temp1);
    // Serial.println("");
  }
  // Serial.print("napiecie odczytane na przelanczniku ");
  // Serial.println(przelancznik);
  // Serial.print("wlancznik ");
  // Serial.println(pstryczek);
}
////////////////////////////
void tryb_muzeum()  /// jak stoisz na wystawie z muzeum to trzymaj tylko SOC w ryzach 40-80%, póki ktoś nie zarząda inaczej
{
  if (czas_zasilZEW > 12 * 3600 * 1000)  ///nie ładuj non stop
  {
    naladowane = 0;  ///zdejmij flage ładowania
    if (mAh < mAh_max * 0.4) {
      ladowanie = 1;       //flaga ładowania, można znowu ładować
      czas_ladowania = 0;  //zeruj zegar ładowania
    }
    ////////
    if (mAh > mAh_max * 0.8)  //wyłącz ładowanie jeśli masz powyzej 80%
    {
      ladowanie = 0;  //trzymaj flagę ładowania w dole, do czasu aż SOC spadnie poniżej 40%
    }
  }

  if (czas_zasilZEW > 72 * 3600 * 1000)  //ew przeładowanie float
  { czas_zasilZEW = 13 * 3600 * 1000; }
}
//////////////////////
void pomiary_ele()  ////pomiary elektryczne INA219
{
  shuntvoltage = ina219.getShuntVoltage_mV();
  busvoltage = ina219.getBusVoltage_V();
  current_mA = ina219.getCurrent_mA();               // prąd ładowania / rozładowywania (przy zerowym poborze wskazuje między -0,4 a -0,6mA)
  power_mW = ina219.getPower_mW();                   //moc do liczenia Wh
  loadvoltage = busvoltage + (shuntvoltage / 1000);  //napięcie pakietu
}
///////////////////////////
void obl_SOC()  ///aktualne SOC baterii
{
  // Serial.print("pojemność przed ");
  // Serial.println(mAh);
  mAh = mAh + (current_mA * czas / 3600000.0);  //pojemność == poprzednia pojemność - prąd*czas w h (AKA Culomb counting)   /// upewnić się że prąd w + i * występuje i to w dobre strony
  // Serial.print("pojemność ");
  // Serial.println(mAh);
  // Serial.print("czas ");
  // Serial.println(czas/3600000.0);

  ///blokada dziwnych akcji z SOC, tak wiem jebać dokładność
  if (mAh > mAh_max) {
    mAh = mAh_max;
  }
  // Serial.print("pojemność po");
  // Serial.println(mAh);
  if (mAh < 1) {
    mAh = 1;
  }
  ///ładowanie i zerowanie SOC dla 3h pod ładowarką
  if (current_mA < 100)  ///upewnić się że prąd w - oznacza ładowanie
  {

    naladowane = 1;
    czas_ladowania = czas_ladowania + czas;  //zliczaj czas ładowania w ms
    czas_zasilZEW = czas_zasilZEW + czas;    //zliczaj czas na zasilaniu zew
    if (czas_ladowania > 3 * 3600 * 1000)    //czas ładowania powyżej 3h zeruje SOC
    {
      mAh = mAh_max;  // "zeruj" SOC
    }
  } else {
    naladowane = 0;
  }  ///flaga naładowania

  SOC = (mAh / mAh_max) * 100;  // w %
  if (SOC>100) //tak profilaktycznie caped do 100%
  {SOC=100;}
  else if (SOC<0)
  {SOC=0;}
  Serial.print("naładowanie % ");
  Serial.println(SOC);
  //zapis do SD co chwilę
  if (obrot > 100) {
   File dataFile = SD.open("/pojemnosc", FILE_WRITE);
if (dataFile) {
  dataFile.seek(dataFile.size());  // ⬅ przesuwasz wskaźnik na koniec pliku (dopisanie)
  dataFile.print(deviceTime);
  dataFile.print(";");
  dataFile.print(mAh);
  dataFile.print(";");
  dataFile.print(current_mA);
  dataFile.print(";");
  dataFile.print(temp1);
  dataFile.print(";");
  dataFile.println(loadvoltage);
  dataFile.close();
}

    
    obrot = 0;  //zeruj licznik pętli
    manageSDOverflow(); //sprawdź czy nie za dużo tego na biednej karcie
  
  }
  obrot = obrot + 1;
  
}
/////////////////////////////////////////////////
void gyro()  ///// gyroskop eventy
{
  // lastAngleX=GyroX;
  //   lastAngleY=GyroY;
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  ///przyspieszenia w m/s2
  AccX = a.acceleration.x;
  AccY = a.acceleration.y;
  AccZ = a.acceleration.z;  ///9,81 domyślne!

  ///prędkość kątowa rad/s
  GyroX = g.gyro.x * czas / 1000;  // Konwersja deg/s na stopnie bez sekund
  GyroY = g.gyro.y * czas / 1000;  // Konwersja deg/s na stopnie
  // GyroZ=g.gyro.z*czas/1000; // Konwersja deg/s na stopnie


  ////filtorowanie
  pitchFromAccel = (atan(AccY / (sqrt(pow(AccX, 2) + pow(AccZ, 2)))) * 180 / PI);  // kąt obrotu w osi Y
  rollFromAccel = (atan(AccX / (sqrt(pow(AccY, 2) + pow(AccZ, 2)))) * 180 / PI);   // kąt obrotu w osi X

  float filtrownik = 0.75;  //pitch/roll GyroFavoring

  _pitch = filtrownik * (pitch0 + GyroY) + (1.00 - filtrownik) * (pitchFromAccel);  //obrót w osi Y oficjalnie
  _roll = filtrownik * (roll0 + GyroX) + (1.00 - filtrownik) * (rollFromAccel);     //obrót w osi X oficjalnie
  pitch0 = _pitch;
  roll0 = _roll;
double pitch, roll;
  pitch = pitch0;
  roll = roll0;

 

  Gyro_mix = max(abs(roll0), abs(pitch0));                                              ///daj max wartość nachyleń i nią wyzwalaj efekty
  Acc_mix = max(abs(AccX - AccXold), max(abs(AccY - AccYold), abs(AccZ - AccZold)));  //daj max wartość z przyspieszeń i nią wyzwalaj efekty


  AccXold = AccX;
  AccYold = AccY;
  AccZold = AccZ;
  // Serial.println("");
  // Serial.println("************");
  // Serial.print("roll ");
  // Serial.println(roll);
  // Serial.println(roll0);
  // Serial.print("pitch ");
  // Serial.println(pitch);
  // Serial.println(pitch0);

  ///temperatura w *C
  temp1 = temp.temperature-3;  //wyjebałem NTC, teraz to robi za bezpiecznik termiczny, zawyża o jakieś 3st.
  Serial.print("temp *C ");
  Serial.println(temp1);
}

void term_bezp()  //// abortuj ładowanie jak jest za gorąco (wodorki nie lubią się przegrzewać)
{
  if (temp1 > 40) {
    digitalWrite(laduj, LOW);
    ladowanie = 0;  /// wyłącz żądanie ładowania
    termika = termika + 1000;
  }                                //odpocznij od ładowania 1000 pętli
  else { termika = termika - 1; }  //jak termicznie jest git to dopiero zjedzie do 0

  if (termika < 0)  //żeby nie wyjść za float + 0 oznacza że jest termicznie bezpiecznie
  { termika = 0; }

  if (termika > 100000000)  //żeby nie wyjść za float z tą karą
  { termika = 100; }
}




/////////////////////
void manageSDOverflow() {
  const int maxLines = 200000000;  // limit adekwatny do 8GB
  const unsigned long SECONDS_IN_WEEK = 604800;

  File original = SD.open("pojemnosc", FILE_READ);
  if (!original) return;

  std::vector<String> preservedLines;
  String lastLine = "";
  unsigned long lastWeekStamp = 0;

  while (original.available()) {
    String line = original.readStringUntil('\n');
    lastLine = line;

    int i1 = line.indexOf(';');
    if (i1 < 1) continue;

    unsigned long timestamp = line.substring(0, i1).toInt();

    // Zapisuj pierwszy rekord z każdego tygodnia
    if (timestamp - lastWeekStamp >= SECONDS_IN_WEEK) {
      preservedLines.push_back(line);
      lastWeekStamp = timestamp;
    }
  }
  original.close();

  // Jeżeli przekroczono limit
  if (preservedLines.size() + 1 > maxLines) {
    Serial.println("Too many lines – recreating trimmed SD log...");

    SD.remove("pojemnosc");
    File trimmed = SD.open("pojemnosc", FILE_WRITE);
    if (!trimmed) return;

    for (const auto& line : preservedLines) {
      trimmed.println(line);
    }
    trimmed.println(lastLine);  // zachowaj ostatnią
    trimmed.close();
  }
}

//////////////////////////LEDy
// ///////////////////////////////


// void startTransition(Mode newMode)
// {
//   previousMode = currentMode;
//   currentMode = newMode;
//   transitionStart = millis();
//   inTransition = true;
//   memcpy(ledsPrev, leds, sizeof(leds)); // zapisanie stanu
// }

// void transitionUpdate()
// {
//   float progress = float(millis() - transitionStart) / transitionDuration;
//   if (progress >= 1.0) {
//     inTransition = false;
//     return;
//   }
// // Wygeneruj aktualny stan nowego efektu (tymczasowo do bufora)
//   CRGB tempLeds[NUM_LEDS];
//   memset(tempLeds, 0, sizeof(tempLeds));

//   switch (currentMode) {
//     case RAINBOW: rainbowEffect(tempLeds); break;
//     case FIRE: fireEffect(tempLeds); break;
//     case CYLON: cylonEffect(tempLeds); break;
//     case TWINKLE: twinkleFOX(tempLeds); break;
//     case COLORWAVES: colorWaves(tempLeds); break;
//     case LAVA: lavaLamp(tempLeds); break;
//     case PACIFICA: pacifica_loop(tempLeds); break;
//     case PULSEB: pulseFadeB(tempLeds); break;
//     case PULSEG: pulseFadeG(tempLeds); break;
//     case PULSER: pulseFadeR(tempLeds); break;
//   }

//   // Blend pomiędzy stanem poprzednim a nowym
//   for (int i = 0; i < NUM_LEDS; i++) {
//     leds[i] = blend(ledsPrev[i], tempLeds[i], progress * 255);
//   }
//   FastLED.show();
// }

///////////////////////////////////////////////warunkowane SOC
void SOC10()  // jak SOC spadnie poniżej 10%
{
  //FastLED.clear(); //zgaś wszystko
  //FastLED.show();
  fill_solid(leds, NUM_LEDS, CRGB(76, 0, 0));  // 30% z maksymalnej wartości 255 (0.3 * 255 ≈ 76)
  FastLED.show();
  blinkStartTime = millis();
  isBlinking = true;
  ///zgaś po ustalonym czasie
  if (isBlinking && millis() - blinkStartTime >= DELAYVAL) {
    isBlinking = false;
  }
}

void SOC5()  // jak SOC spadnie poniżej 5%
{
  // FastLED.clear(); //zgaś wszystko
  // FastLED.show();
  for (int i = 0; i < NUM_LEDS - 1; i = i + 5)  //co 5 zapal na czerwono
  {
    leds[i] = CRGB(150, 0, 0);  ///napierdalaj czerowym
  }
  FastLED.show();
  blinkStartTime = millis();
  isBlinking = true;
  if (isBlinking && millis() - blinkStartTime >= DELAYVAL * 2) {
    isBlinking = false;
  }
}
void SOC100()  // jak SOC powyżej 99% podczas ładowania
{
  // fill_solid(leds, NUM_LEDS, CRGB(0, 85, 0));  // 30% z maksymalnej wartości 255 (0.3 * 255 ≈ 76)
  // FastLED.clear(); //zgaś wszystko
  // FastLED.show();
  // delay(DELAYVAL/2);
  fill_solid(leds, NUM_LEDS, CRGB(0, 85, 0));  // 30% z maksymalnej wartości 255 (0.3 * 255 ≈ 76)
  FastLED.show();
  blinkStartTime = millis();
  isBlinking = true;
  if (isBlinking && millis() - blinkStartTime >= DELAYVAL*2) {
    isBlinking = false;
  }
}
////////////////////
void DISCO()  ///świeci na Acc_T w zależności od przyspieszeń
{ FastLED.clear(); //czyść żeby mrygało jak głupie
  FastLED.show();
  ///go random
  for (int i = 0; i < NUM_LEDS ; i++) {
    int posX = random(NUM_LEDS);
    leds[posX] = CHSV(95, 255, random(20, 200));  ///zielony X
    // leds[posX] = CRGB::Black;                    // Zgaś tę diodę przy następnym wywołaniu
  }
  for (int i = 0; i < NUM_LEDS ; i++) {
    int posZ = random(NUM_LEDS);
    leds[posZ] = CHSV(5, 255, random(20, 200));  //czerwony Z
    // leds[posZ] = CRGB::Black;                   // Zgaś tę diodę przy następnym wywołaniu
   }
  for (int i = 0; i < NUM_LEDS ; i++) {
    int posY = random(NUM_LEDS);
    leds[posY] = CHSV(160, 255, random(20, 200));  //niebieski Y
    // leds[posY] = CRGB::Black;                     // Zgaś tę diodę przy następnym wywołaniu
  }
  FastLED.show();
  // blinkStartTime = millis();
  // isBlinking = true;
  // if (isBlinking && millis() - blinkStartTime >= Acc_T) {
  //   isBlinking = false;
  // }
}
////////////////
void FullTilt()  //świeci w zależności od przechyłu (500* niby max)
{ 
  // // Normalizacja koloru do zakresu 0–255
  green = map(pitch0, 0, 100, 20, 255);
  blue = map(roll0, 0, 100, 20, 255);
  red = 60;  ///obrót w osi Z to masakra do wyzerowania
  // Ustawienie jasności zależnie od Gyro_mix
  brightness_Gyro = map(Gyro_mix, 10, 90, 50, 150);  // dolny limit 50 by nie wygasić całkowicie

  fill_solid(leds, NUM_LEDS, CRGB(red, green, blue));
  FastLED.setBrightness(brightness_Gyro);
  
  FastLED.show();

  Serial.println("");
  Serial.println("************");
  Serial.print("roll ");

  Serial.println(roll0);
  Serial.print("pitch ");

  Serial.println(pitch0);
  Serial.println("************");
}

///////////////////////////////////////////////////////////////
void rainbowEffect() {
  static uint8_t hue = 0;
  static uint8_t pulse = 0;
  static bool pulseUp = true;
  

  fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255));
  hue++; // powolna zmiana koloru przez całą tęczę

  // Efekt pulsowania jasności
  if (pulseUp) {
    pulse++;
    if (pulse >= 255) pulseUp = false;
  } else {
    pulse--;
    if (pulse <= 50) pulseUp = true;
  }

  FastLED.setBrightness(scale8(BRIGHTNESS_MAX, pulse));
  FastLED.show();
}

/////////////////////////////////////twinkleFOX
void twinkleFOX() {
  fadeToBlackBy(leds, NUM_LEDS, 20);
  int pos = random(NUM_LEDS);
  leds[pos] = CRGB::White;
  FastLED.show();
}

///////////////////////////////////////////CYLON
void cylonEffect() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);

  int greenPos = (cylonPos + NUM_LEDS / 3) % NUM_LEDS;
  int bluePos = (cylonPos + (2 * NUM_LEDS) / 3) % NUM_LEDS;

  leds[cylonPos] = CRGB::Red;
  leds[greenPos] = CRGB::Green;
  leds[bluePos] = CRGB::Blue;

  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();

  cylonPos += cylonDir;
  if (cylonPos == 0 || cylonPos == NUM_LEDS - 1) cylonDir *= -1;
}

///////////////////////////////////////////////
DEFINE_GRADIENT_PALETTE( myLavaPalette ) {
  0,   0,  0,  0,    // black
  64, 120,  0,  0,   // dark red
  128, 255, 80,  0,  // orange
  192, 255,160,  0,  // yellow-orange
  255, 200,  0,  0   // red
};
CRGBPalette16 lavaPalette = myLavaPalette;

void lavaLamp() {
  static uint8_t heat[NUM_LEDS];
  static uint8_t startIndex = 0;
  for (int i = 0; i < NUM_LEDS; i++) {
    heat[i] = qsub8(heat[i], random8(0, 4));
    heat[i] = qadd8(heat[i], random8(0, 3));
    uint8_t colorindex = scale8(heat[i], 240);
    leds[i] = ColorFromPalette(lavaPalette, startIndex + colorindex, 255, LINEARBLEND);
  }
  startIndex += 1;
  FastLED.show();
}
/////////////////////////////////////////
void pulseFadeB() {
  if (pulseUp) {
    pulseBrightness += 5;
    if (pulseBrightness >= 255) pulseUp = false;
  } else {
    pulseBrightness -= 5;
    if (pulseBrightness <= 10) pulseUp = true;
  }
  fill_solid(leds, NUM_LEDS, CHSV(160, 255, pulseBrightness));
  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();
}
///////////////////////////////////////
void pulseFadeG() {
  if (pulseUp) {
    pulseBrightness += 5;
    if (pulseBrightness >= 255) pulseUp = false;
  } else {
    pulseBrightness -= 5;
    if (pulseBrightness <= 10) pulseUp = true;
  }
  fill_solid(leds, NUM_LEDS, CHSV(95, 255, pulseBrightness));
  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();
}
////////////////////////////////////
void pulseFadeR() {
  if (pulseUp) {
    pulseBrightness += 5;
    if (pulseBrightness >= 255) pulseUp = false;
  } else {
    pulseBrightness -= 5;
    if (pulseBrightness <= 10) pulseUp = true;
  }
  fill_solid(leds, NUM_LEDS, CHSV(5, 255, pulseBrightness));
  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();
}
////////////////////////////////////////
void fireEffect() {
  static byte heat[NUM_LEDS];

  for (int i = 0; i < NUM_LEDS; i++) {
    heat[i] = qsub8(heat[i], random8(0, 50));          // chłodzenie
    heat[i] = qadd8(heat[i], random8(0, 70));          // losowe rozgrzanie
    uint8_t t192 = scale8(heat[i], 192);               // skaluj temperaturę
    uint8_t heatramp = t192 & 0x3F;                    // 0-63
    heatramp <<= 2;                                    // 0-252

    if (t192 > 128) {
      leds[i] = CRGB(255, heatramp, 0);                // gorący ogień
    } else if (t192 > 64) {
      leds[i] = CRGB(heatramp, heatramp / 2, 0);       // średni ogień
    } else {
      leds[i] = CRGB(heatramp / 2, 0, 0);              // chłodny żar
    }
  }

  FastLED.setBrightness(BRIGHTNESS_MAX);
   FastLED.show();
}
///////////////////////////////////
void colorWaves() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV((millis() / 10 + i * 8) % 255, 255, 255);
  }
  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();
}
//////////////////////////////
void bubbleEffect() {
  static uint8_t bubbleCooldown = 0;
  static CHSV bubbleHue = CHSV(random8(), 255, 255);

  // Przesuń całą tablicę w górę (od końca do początku)
  for (int i = NUM_LEDS - 1; i > 0; i--) {
    leds[i] = leds[i - 1];
    leds[i].fadeToBlackBy(10);  // delikatne znikanie ogona
  }

  // Miejsce startu bąbelka (na dole pierścienia)
  if (bubbleCooldown == 0 && random8() < 50) {
    bubbleHue = CHSV(random8(), 255, 255);
    leds[0] = bubbleHue;
    bubbleCooldown = random8(10, 30);  // rzadsze generowanie
  } else {
    leds[0].fadeToBlackBy(40);  // powolne znikanie początku
    if (bubbleCooldown > 0) bubbleCooldown--;
  }

  FastLED.setBrightness(BRIGHTNESS_MAX);
  FastLED.show();
}
/////////////////////////////////
CRGBPalette16 pacifica_palette_1 = 
    { 0x000507, 0x000409, 0x00030B, 0x00030D, 0x000210, 0x000212, 0x000114, 0x000117, 
      0x000019, 0x00001C, 0x000026, 0x000031, 0x00003B, 0x000046, 0x14554B, 0x28AA50 };
CRGBPalette16 pacifica_palette_2 = 
    { 0x000507, 0x000409, 0x00030B, 0x00030D, 0x000210, 0x000212, 0x000114, 0x000117, 
      0x000019, 0x00001C, 0x000026, 0x000031, 0x00003B, 0x000046, 0x0C5F52, 0x19BE5F };
CRGBPalette16 pacifica_palette_3 = 
    { 0x000208, 0x00030E, 0x000514, 0x00061A, 0x000820, 0x000927, 0x000B2D, 0x000C33, 
      0x000E39, 0x001040, 0x001450, 0x001860, 0x001C70, 0x002080, 0x1040BF, 0x2060FF };
 
void pacifica_loop()
{
  // Increment the four "color index start" counters, one for each wave layer.
  // Each is incremented at a different speed, and the speeds vary over time.
  static uint16_t sCIStart1, sCIStart2, sCIStart3, sCIStart4;
  static uint32_t sLastms = 0;
  uint32_t ms = GET_MILLIS();
  uint32_t deltams = ms - sLastms;
  sLastms = ms;
  uint16_t speedfactor1 = beatsin16(3, 179, 269);
  uint16_t speedfactor2 = beatsin16(4, 179, 269);
  uint32_t deltams1 = (deltams * speedfactor1) / 256;
  uint32_t deltams2 = (deltams * speedfactor2) / 256;
  uint32_t deltams21 = (deltams1 + deltams2) / 2;
  sCIStart1 += (deltams1 * beatsin88(1011,10,13));
  sCIStart2 -= (deltams21 * beatsin88(777,8,11));
  sCIStart3 -= (deltams1 * beatsin88(501,5,7));
  sCIStart4 -= (deltams2 * beatsin88(257,4,6));
 
  // Clear out the LED array to a dim background blue-green
  fill_solid( leds, NUM_LEDS, CRGB( 2, 6, 10));
 
  // Render each of four layers, with different scales and speeds, that vary over time
  pacifica_one_layer( pacifica_palette_1, sCIStart1, beatsin16( 3, 11 * 256, 14 * 256), beatsin8( 10, 70, 130), 0-beat16( 301) );
  pacifica_one_layer( pacifica_palette_2, sCIStart2, beatsin16( 4,  6 * 256,  9 * 256), beatsin8( 17, 40,  80), beat16( 401) );
  pacifica_one_layer( pacifica_palette_3, sCIStart3, 6 * 256, beatsin8( 9, 10,38), 0-beat16(503));
  pacifica_one_layer( pacifica_palette_3, sCIStart4, 5 * 256, beatsin8( 8, 10,28), beat16(601));
 
  // Add brighter 'whitecaps' where the waves lines up more
  pacifica_add_whitecaps();
 
  // Deepen the blues and greens a bit
  pacifica_deepen_colors();
   FastLED.show();
}
 
// Add one layer of waves into the led array
void pacifica_one_layer( CRGBPalette16& p, uint16_t cistart, uint16_t wavescale, uint8_t bri, uint16_t ioff)
{
  uint16_t ci = cistart;
  uint16_t waveangle = ioff;
  uint16_t wavescale_half = (wavescale / 2) + 20;
  for( uint16_t i = 0; i < NUM_LEDS; i++) {
    waveangle += 250;
    uint16_t s16 = sin16( waveangle ) + 32768;
    uint16_t cs = scale16( s16 , wavescale_half ) + wavescale_half;
    ci += cs;
    uint16_t sindex16 = sin16( ci) + 32768;
    uint8_t sindex8 = scale16( sindex16, 240);
    CRGB c = ColorFromPalette( p, sindex8, bri, LINEARBLEND);
    leds[i] += c;
  }
}
 
// Add extra 'white' to areas where the four layers of light have lined up brightly
void pacifica_add_whitecaps()
{
  uint8_t basethreshold = beatsin8( 9, 55, 65);
  uint8_t wave = beat8( 7 );
  
  for( uint16_t i = 0; i < NUM_LEDS; i++) {
    uint8_t threshold = scale8( sin8( wave), 20) + basethreshold;
    wave += 7;
    uint8_t l = leds[i].getAverageLight();
    if( l > threshold) {
      uint8_t overage = l - threshold;
      uint8_t overage2 = qadd8( overage, overage);
      leds[i] += CRGB( overage, overage2, qadd8( overage2, overage2));
    }
  }
}
 
// Deepen the blues and greens
void pacifica_deepen_colors()
{
  for( uint16_t i = 0; i < NUM_LEDS; i++) {
    leds[i].blue = scale8( leds[i].blue,  145); 
    leds[i].green= scale8( leds[i].green, 200); 
    leds[i] |= CRGB( 2, 5, 7);
  }
}