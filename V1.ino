#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

Adafruit_MPU6050 mpu;
Adafruit_INA219 ina219;

#define LED  2   // The ESP8266 pin that connects to WS2812B
#define LEDOW   34 // liczba ledów to 35, ale adresy od 0

Adafruit_NeoPixel pixels(LEDOW, LED, NEO_GRB + NEO_KHZ800); //ustawienie LED
#define DELAYVAL 500 // Time (in milliseconds) to pause 

#define laduj 0 //enable ładowania ogniw
#define wlancz 13 // pozycja włącznika


int pstryczek=1; // stan włącznika
int ladowanie=1; // 0 == nie ładuj; 1== żądanie ładowania
float termika=0; // czy jest bezpiecznie temicznie, 0==tak, >1 za ciepło
float temp1=20; //temp z ntc
int Ro = 10, B = 3950; //Nominal resistance 10K, Beta constant
float Rseries = 9.2;// R Series resistor 10K
float To = 298.15; // Nominal Temperature
float Vin=4.92; //napięcie "5V"

//ina219
  float shuntvoltage = 0;
  float busvoltage = 0;
  float current_mA = 0;
  float loadvoltage = 0;
  float power_mW = 0;

//////////////stan baterii
float SOC; // poziom naładowania %; 2,5V==0; 4,2V==100% 
double mAh; // poziom naładowania w mAh
long prev_milis=0;
long czas=0; // czas na potrzeby liczenia
long czas_ladowania=0;
long czas_zasilZEW=0;

//31 wartości do identyfikowania *początkowego* SOC
float napiecia[]={4.2, 4.06, 4.03, 3.99, 3.97, 3.94, 3.93, 3.91, 3.90, 3.89, 3.88, 3.8799, 3.8796, 3.873, 3.871, 3.870, 3.868, 3.867, 3.866, 3.865, 3.864, 3.862, 3.8638, 3.79, 3.43, 3.38, 3.14, 3.03, 2.937, 2.7, 2.4}; //progi napięc w V
float mAh_OCV[]={0,0.001,21.54,78.01, 152.30,229.56,321.693,452.45,586.18,722.88,868.49,1026.02,1141.91,1284.54,1415.36,1543.09,1685.73,1795.69,1899.782,2015.67,2122.58,2214.71,2312.77,2384.11,2443.53,2485.14,2523.74,2547.54,2556.463,2571.322,2575}; //progi pojemności mAh
int mAh_max=2575; //max SOC - zmierzyć kiedyś

void setup() {
pixels.begin(); //start LED
 Serial.begin(9600);


////////ina219 miernik prądu
ina219.begin();
delay(500);
///początkowy SOC
busvoltage = ina219.getBusVoltage_V(); 
for (int i = 0; i < 30; i = i + 1)
{
  if (busvoltage >= napiecia[i])
  {
  SOC=2575/mAh_OCV[i]*100; //% SOC
  mAh=mAh_OCV[i]; //SOC mAh
  break;  //to mi wyskoczy z pętli for??
  }
}

/////////żyroskop konfig
mpu.begin();


   mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  // Serial.print("Accelerometer range set to: ");
  switch (mpu.getAccelerometerRange()) {
  // case MPU6050_RANGE_2_G:
  //   // Serial.println("+-2G");
  //   break;
  case MPU6050_RANGE_4_G:
    // Serial.println("+-4G");
    break;
  // case MPU6050_RANGE_8_G:
  //   // Serial.println("+-8G");
  //   break;
  // case MPU6050_RANGE_16_G:
  //   // Serial.println("+-16G");
  //   break;
  }
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  // Serial.print("Gyro range set to: ");
  switch (mpu.getGyroRange()) {
  case MPU6050_RANGE_250_DEG:
    // Serial.println("+- 250 deg/s");
    break;
  // case MPU6050_RANGE_500_DEG:
  //   // Serial.println("+- 500 deg/s");
  //   break;
  // case MPU6050_RANGE_1000_DEG:
  //   // Serial.println("+- 1000 deg/s");
  //   break;
  // case MPU6050_RANGE_2000_DEG:
  //   // Serial.println("+- 2000 deg/s");
  //   break;
  }

  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);
  // Serial.print("Filter bandwidth set to: ");
  switch (mpu.getFilterBandwidth()) {
  // case MPU6050_BAND_260_HZ:
  //   // Serial.println("260 Hz");
  //   break;
  // case MPU6050_BAND_184_HZ:
  //   // Serial.println("184 Hz");
  //   break;
  // case MPU6050_BAND_94_HZ:
  //   // Serial.println("94 Hz");
  //   break;
  // case MPU6050_BAND_44_HZ:
  //   // Serial.println("44 Hz");
  //   break;
  case MPU6050_BAND_21_HZ:
    // Serial.println("21 Hz");
    break;
  // case MPU6050_BAND_10_HZ:
  //   // Serial.println("10 Hz");
  //   break;
  // case MPU6050_BAND_5_HZ:
  //   // Serial.println("5 Hz");
    // break;
  }

}

void loop() {
czas=millis()-prev_milis;  //czas wykonania pętli
prev_milis=czas; //zliczanie czasu pętli

/////////INA219
pomiary_ele();
obl_SOC();

//////////ładowanie
temp1= Read_NTC10k(); //temp z NTC
//Serial.println((String)"Temperature in celsius    :" + temp1 + "°C");
//Serial.println(" ");

//kolejność wykonywania ważna, najpierw żądania dbające os SOH ogniw, bezpieczeństwo termiczne tuż przed ładowaniem
tryb_muzeum(); //odpowiada za nie ładowanie permanente przy dłuższym postoju na zasilaniu zew.
term_bezp();
tryb_ladowania(); // odpowiada za ładowanie

////sygnalizacja niskiego stanu ogniw
bateria_pada();  



///////żyroskop
gyro();




pixels.clear(); // Set all pixel colors to 'off'

  // The first NeoPixel in a strand is #0, second is 1, all the way up
  // to the count of pixels minus one.
  for(int i=0; i<LEDOW; i++) { // For each pixel...

    // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
    // Here we're using a moderately bright green color:
    pixels.setPixelColor(i, pixels.Color(0, 50, 0));

    pixels.show();   // Send the updated pixel colors to the hardware.

    delay(DELAYVAL); // Pause before next pass through loop
  }



}



///////////////////////////////////////////////////////////////////////////////////////////////

void tryb_ladowania() ///laduj==1 żądanie ładowania; ładuj == 0 nie ładujemy
{
pstryczek=digitalRead(wlancz);
if(termika==0) ///jak termicznie bezpiecznie, dopiero myśl o ładowaniu
{
  if (pstryczek == 0)   ///jak wyłącznik jest w pozycji off, to dopiero wolno ładować (inaczej jebie zwarciem) 
  {
    if (ladowanie == 1)  //żądanie ładowania =1; nie ładuj=0
    {
      digitalWrite(laduj,HIGH);
    }
    else
    {
    digitalWrite(laduj,LOW);
    }
  }
  else
  {
  digitalWrite(laduj,LOW);
  ladowanie=0;
  }
}
}

void tryb_muzeum()  /// jak stoisz na wystawie z muzeum to trzymaj tylko SOC w ryzach 40-80%, póki ktoś nie zarząda inaczej
{
  if (czas_zasilZEW>12*3600*1000) ///nie ładuj non stop
{
  if (mAh<mAh_max*0.4)
  {
  ladowanie=1;   //flaga ładowania, można znowu ładować
  czas_ladowania=0; //zeruj zegar ładowania
  }
  ////////
  if(mAh>mAh_max*0.8) //wyłącz ładowanie jeśli masz powyzej 80%
  {
  ladowanie=0;   //trzymaj flagę ładowania w dole, do czasu aż SOC spadnie poniżej 40%
  }
}

if(czas_zasilZEW>72*3600*1000)  //ew przeładowanie float
{czas_zasilZEW=13*3600*1000;}

}

void pomiary_ele() ////pomiary elektryczne INA219
{
  shuntvoltage = ina219.getShuntVoltage_mV();
  busvoltage = ina219.getBusVoltage_V();     
  current_mA = ina219.getCurrent_mA();  // prąd ładowania / rozładowywania
  power_mW = ina219.getPower_mW();  //moc do liczenia Wh
  loadvoltage = busvoltage + (shuntvoltage / 1000); //napięcie pakietu 
}

void obl_SOC()  ///aktualne SOC baterii 
{
mAh=mAh+(current_mA*czas*1000/3600); //pojemność == poprzednia pojemność - prąd*czas w h (AKA Culomb counting)   /// upewnić się że prąd w + i * występuje i to w dobre strony

///blokada dziwnych akcji z SOC, tak wiem jebać dokładność
if(mAh>mAh_max)
{
  mAh=mAh_max;
}
if(mAh<0)
{
  mAh=0;
}
///ładowanie i zerowanie SOC dla 3h pod ładowarką
if(current_mA<0) ///upewnić się że prąd w - oznacza ładowanie
{
  if(laduj==0) ///jak masz prąd ładowania, a nie wystawiałeś sygnału do ładowania, daj zwrotkę o błędzie 
  {
  Serial.println((String)"ładujesz a nie powinieneś");
  Serial.println(" ");
  }

  czas_ladowania=czas_ladowania+czas; //zliczaj czas ładowania
  czas_zasilZEW=czas_zasilZEW+czas; //zliczaj czas na zasilaniu zew
  if(czas_ladowania>3*3600*1000) //czas ładowania powyżej 3h zeruje SOC
  {
    mAh=mAh_max; // "zeruj" SOC
  }
}

SOC=2575/mAh*100; // w %
}

void gyro()///// gyroskop eventy
{
  sensors_event_t a, g, temp;
mpu.getEvent(&a, &g, &temp);

///przyspieszenia w m/s2
// a.acceleration.x
// a.acceleration.y
// a.acceleration.z      ///9,81 domyślne!

///prędkość kątowa rad/s
// g.gyro.x
// g.gyro.y
// g.gyro.z

///temperatura w *C
// temp.temperature
///
}

void term_bezp()  //// abortuj ładowanie jak jest za gorąco (wodorki nie lubią się przegrzewać)
{
  if (temp1>40)
  {digitalWrite(laduj, LOW);
  ladowanie=0; /// wyłącz żądanie ładowania
  termika=termika+1000;}  //odpocznij od ładowania 1000 pętli 
  else
  {termika=termika-1;} //jak termicznie jest git to dopiero zjedzie do 0

  if (termika<0) //żeby nie wyjść za float + 0 oznacza że jest termicznie bezpiecznie
  {termika=0;}

  if (termika>100000000) //żeby nie wyjść za float z tą karą
  {termika=100;}
}

float Read_NTC10k() /// czujnik temperatury -> dokładność kartofla
{
float Vi = analogRead(A0) * (Vin / 1023.0);
  //Convert voltage measured to resistance value
  //All Resistance are in kilo ohms.
  float R = ((Vin-Vi)*Rseries) / Vi;
  /*Use R value in steinhart and hart equation
    Calculate temperature value in kelvin*/
  float T =  1 / ((1 / To) + ((log(R / Ro)) / B));
  float tempC = T - 273.15-5; // Converting kelvin to celsius i dla tego egzemplarza -5st xD
  return tempC;
}



void bateria_pada()  ///jak poziom SOC spada poniżej 10% zaczniej drzeć mordę
{
  if (mAh<mAh_max*0.1)  //dla 10% wstępne ostrzeżenie
  {
    for(int i=0; i<LEDOW; i++)
    {
    // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
    pixels.setPixelColor(i, pixels.Color(80, 0, 0)); //średni czerwony na wszystkie
    pixels.show();   

    }
 delay(DELAYVAL); // wyświetlaj tak długo
  }
  
  if (mAh<mAh_max*0.05)   //jest źle, gdzie mój prąd!
  {
    for(int i=0; i<LEDOW-5; i=i+5)
    {
    // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
    pixels.setPixelColor(i, pixels.Color(150, 0, 0));
    pixels.show();  

    }
 delay(DELAYVAL*2); //wyświetlaj tak długo
  }
}