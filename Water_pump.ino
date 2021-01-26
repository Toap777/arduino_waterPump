#include <Servo.h>


//configuration of standard values

const int wateringStartThreshold = 40; //the system starts watering the plant if wetness is above or below this limit.
const int wateringStopThreshold = 60; // the system stops watering the plant if wetness is above or equal to this limit.
const long maxDurationPumpingInMs= 10000;

//dynamic values
//self adaptive.
int minValueSensor1;  
int minValueSensor2;
int maxValueSensor1;  
int maxValueSensor2;

int measurementSensor1 = 0;
float averageSensor1 = 0.0;
int measurementSensor2 = 0; 
float averageSensor2 = 0.0;

//Pins
int PUMP_CONTROL_PIN = 5;
int MOISTURE_SENSOR1_PIN = A0;
int MOISTURE_SENSOR2_PIN = A1;
int SERVO1_PIN = A2;
int SERVO2_PIN = A3;
int TOGGLE_SYSTEM_PIN = A5;

//devices
Servo Ventil1;
Servo Ventil2;

//system state logic
int systemState = 0; //-2 error, -1 standby, 0 idle,1 pump running

//states: -1 too dry, 0 transit zone, 1 ideal
int wetnessStateSensor1 = 0;
int wetnessStateSensor2 = 0;

long timer = 0;
bool timerStarted = false;


void setup() {
  Serial.begin(9600); // open serial port, set the baud rate to 9600 bps
  Ventil1.attach(SERVO1_PIN); 
  Ventil2.attach(SERVO2_PIN); 
  
  int startValueS1 = analogRead(MOISTURE_SENSOR1_PIN);
  int startValueS2 = analogRead(MOISTURE_SENSOR2_PIN);
  minValueSensor1 = startValueS1 -100;
  minValueSensor2 = startValueS2 -100;
  maxValueSensor1 = startValueS1 +100;
  maxValueSensor2 = startValueS2 +100;
  averageSensor1 = startValueS1;
  averageSensor2 = startValueS2;
  pinMode(TOGGLE_SYSTEM_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  
}

void loop() {
  Measure();
  AdaptSytem();
}

void Measure(){
measurementSensor1 = analogRead(MOISTURE_SENSOR1_PIN);  //put Sensor insert into soil
measurementSensor2 = analogRead(MOISTURE_SENSOR2_PIN);
MovingAverage(averageSensor1, measurementSensor1, 10); // 9/10 des alten Wertes.
MovingAverage(averageSensor2, measurementSensor2, 10);

//Serial.println("Sensor1: Average "+ String(averageSensor1));
//Serial.println("Sensor2: Average " +String(averageSensor2));

//Adapt sensorBounds
if(averageSensor1 > maxValueSensor1){
  maxValueSensor1 = averageSensor1;
}

if(averageSensor1 < minValueSensor1){
  minValueSensor1 = averageSensor1;
}

if(averageSensor2 > maxValueSensor2){
  maxValueSensor2 = averageSensor2;
}

if(averageSensor2 < minValueSensor2){
  minValueSensor2 = averageSensor2;
}

//Serial.println("UpperBound: "+ String(maxValueSensor1));
//Serial.println("LowerBound " +String(minValueSensor1));

//Convert to percentage
int wetnessSensor1 = round(map(averageSensor1, minValueSensor1, maxValueSensor1, 0, 100));
int wetnessSensor2 = round(map(averageSensor2, minValueSensor2, maxValueSensor2, 0, 100));

//Normalize percentage values to a range from 0 to 100.

NormalizePercentage(wetnessSensor1);
NormalizePercentage(wetnessSensor2);

//invert percentage => result: 100 % means 100% wet
wetnessSensor1 = 100 -wetnessSensor1;
wetnessSensor2 = 100 -wetnessSensor2;

//states: -1 too dry, 0 transit zone, 1 ideal
wetnessStateSensor1 = CalculateWetnessState(wetnessSensor1);
wetnessStateSensor2 = CalculateWetnessState(wetnessSensor2);

Serial.println("Sensor1 - Wetness: "+ String(wetnessSensor1));
Serial.println("Sensor2 - Wetness " +String(wetnessSensor2));
}

void AdaptSytem(){
delay(200);
//determine if should switch to standby state.
bool isOn = !digitalRead(TOGGLE_SYSTEM_PIN); //delivers 0 if curcuit is closed and 1 if not.
Serial.println("is on: " + String(isOn));
if(!isOn){
  StopSystem();
  systemState = -1;
}

//-2 error, -1 standby, 0 idle,1 pump running
switch(systemState){
  case -2:
  {
    //Blink on error.
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);

  }
  break;

  
  case -1:
  {  
    //Reset system.
    if(isOn){
      systemState = 0;
    }

  }
  break;
  case 0:
  {
    bool ventilOpen = false;
    if(wetnessStateSensor1 == -1){
      OpenVentil(Ventil1);
      ventilOpen = true;
        Serial.println("Ventil 1 open");
    }
    if(wetnessStateSensor2 == -1){
      OpenVentil(Ventil2);
      ventilOpen = true;
        Serial.println("Ventil 2 open");
    }
    if(ventilOpen){
      StartPump();
      systemState = 1;
      Serial.println("pumpStart");
    }
    delay(300);    
  }
  break;
  
  case 1:
  {
    //Check timer
    if(!timerStarted){
      StartTimer();
    }
    else if(IsTimeLimitReached(maxDurationPumpingInMs)){
      StopSystem();
      systemState = -2;
    } 
    if(wetnessStateSensor1 == 1 && wetnessStateSensor2 == 1){
      StopPump();
      systemState = 0;
    }
    if(wetnessStateSensor1 == 1){
      CloseVentil(Ventil1);
    }
    if(wetnessStateSensor2 == 1){
      CloseVentil(Ventil2);
    } 
  }
  break;
}
Serial.println("System state:  " + String(systemState));
}


///////////////// HELPER functions //////////////////////////////////
//States: -1 too dry, 0 transit zone, 1 ideal
int CalculateWetnessState(int wetnessPercentage){
  if(wetnessPercentage >= wateringStopThreshold){
    Serial.println("Returned ideal");
  return 1;
}
else if(wetnessPercentage <= wateringStartThreshold){
  Serial.println("Returned too dry");
  return -1;
}
else{
  Serial.println("Returned transit zone");
 return 0;
}

}


void StartTimer(){
  timer = millis();
  timerStarted = true;
}

bool IsTimeLimitReached(long myTimeout){
  if (millis() > myTimeout + timer) {
    timerStarted = false;
    return true;
  }
  else{
    return false;
  }
}

void StopSystem(){
  StopPump();
  CloseVentil(Ventil1);
  CloseVentil(Ventil2);
}

void OpenVentil(Servo ventil){
  ventil.write(170);
}

void CloseVentil(Servo ventil){
  ventil.write(0);
}

void StopPump(){
  digitalWrite(PUMP_CONTROL_PIN,LOW);
}

void StartPump(){
   digitalWrite(PUMP_CONTROL_PIN,HIGH);
}


void NormalizePercentage(int &percentage){
  if(percentage > 100)
{
  percentage = 100;
}
  else if(percentage <0)
{
  percentage =0;
 
}
}





/*************************************************************************************************
** Funktion MovingAverage()  by GuntherB            2014                                               
**************************************************************************************************
** Bildet einen Tiefpassfilter (RC-Glied) nach.           
** FF = Filterfaktor;  Tau = FF / Aufruffrequenz            
**                            
**  Input: FiltVal der gefilterte Wert, NewVal der neue gelesene Wert; FF Filterfaktor    
**  Output: FiltVal                   
**  genutzte Globale Variablen:   keine             
**************************************************************************************************/
void MovingAverage(float &FiltVal, int NewVal, int FF){
  FiltVal= ((FiltVal * FF) + NewVal) / (FF +1); 
}
