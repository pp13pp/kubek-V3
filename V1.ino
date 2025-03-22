#include <Adafruit_NeoPixel.h>
#include <math.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

Adafruit_MPU6050 mpu;
Adafruit_INA219 ina219;

#define LED  2   // The ESP8266 pin that connects to WS2812B
#define LEDOW   5 // The number of LEDs (pixels) on WS2812B

Adafruit_NeoPixel pixels(LEDOW, LED, NEO_GRB + NEO_KHZ800); //ustawienie LED
#define DELAYVAL 500 // Time (in milliseconds) to pause between pixels

#define laduj 0 //enable ładowania ogniw
#define wlancz 13 // pozycja włącznika


int pstryczek=1; // stan włącznika
int ladowanie=0; // stan ładowania
int termika=0; // czy jest bezpiecznie temicznie, 0==tak, 1== za ciepło
float temp=20;
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
float mAh; // poziom naładowania w mAh
long long prev_milis=0;
long long czas=0; // czas na potrzeby liczenia

//31 wartości do identyfikowania *początkowego* SOC
float napiecia[]={4.2, 4.06, 4.03, 3.99, 3.97, 3.94, 3.93, 3.91, 3.90, 3.89, 3.88, 3.8799, 3.8796, 3.873, 3.871, 3.870, 3.868, 3.867, 3.866, 3.865, 3.864, 3.862, 3.8638, 3.79, 3.43, 3.38, 3.14, 3.03, 2.937, 2.7, 2.4}; //progi napięc w V
float mAh_OCV[]={0,0.001,21.54,78.01, 152.30,229.56,321.693,452.45,586.18,722.88,868.49,1026.02,1141.91,1284.54,1415.36,1543.09,1685.73,1795.69,1899.782,2015.67,2122.58,2214.71,2312.77,2384.11,2443.53,2485.14,2523.74,2547.54,2556.463,2571.322,2575}; //progi pojemności mAh


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

/////////INA219
pomiary_ele();
obl_SOC();


//////////ładowanie
temp = Read_NTC10k();
//Serial.println((String)"Temperature in celsius    :" + temp + "°C");
//Serial.println(" ");
term_bezp();
pstryczek=digitalRead(wlancz);
// if (termika == 0){
  if (pstryczek == 0)
  {
    if (ladowanie == 0)
    {
      digitalWrite(laduj,HIGH);
      ladowanie=1;
    }
  }
  else
  { digitalWrite(laduj,LOW);
  ladowanie=0;
  }
// }



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
czas=millis()-prev_milis;
mAh=mAh+(current_mA*czas*1000/3600); //pojemność == poprzednia pojemność - prąd*czas w h (AKA Culomb counting)
prev_milis=czas;
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
  if (temp>40)
  {digitalWrite(laduj, LOW);
  ladowanie=0;
  termika=1;} 
  // else
  // {termika=0;} 
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