//Для датчиков температуры
#include <OneWire.h>
#include <DallasTemperature.h>
//Для серво-мотора тепло-холод
#include <Servo.h>
#include <ServoSmooth.h>
//Для CAN шины
/*#include <mcp2515.h>
#include <SPI.h>*/
//Для PID регулятора
#include <GyverPID.h>
//Для работы с памятью
#include <EEPROM.h>

/*
  Command List:
  403 = запрос версии прошивки
  404 = сохранение
  405 = сброс сохранений
  908 = сигнализирует о подключение устройства(нужно сообщить все показатели)
  909 = автоматический режим {
    01 ** = устанавливает желаемую температуру
    11 *** = установка скорости вентилятора(0-100)
    12 *(0 или 1) = всегда включен вентилятор
    13 = выключить ручной режим для вентилятора
    22 *** = установка положения тепло-холод(0-100), где 50 середина
    23 = выключить ручной режим для заслонки тепло-холод
  }
  910 = ручной режим {
    11 *** = установка скорости вентилятора(0-100)
    12 *(0 или 1) = всегда включен вентилятор
    22 *** = установка положения тепло-холод(0-100), где 50 середина
  }
  911 = сервисный режим {
    // 1** - связан с вентилятором
    11 ** = тест скорости вентилятора (абсолютный)
    12 ** = тест скорости вентилятора (относительный)(с учетом минимума)
    13 *(0 или 1) = всегда включен вентилятор
    14 *** = установка минимальной скорости вентилятора(max 255)

    // 2** - связан с серво-мотором, тепло-холод
    21 ** = тест положения тепло-холод (абсолютный)
    22 ** = тест положения тепло-холод (относительный)(с учетом ограничений)
    23 ** = тест положения тепло-холод в мкс
    24 *** = установка максимального положения тепло-холод
    25 ** = установить метвую зону в градусах сервопривода

    // 3** - связан с серво-мотором, направление потока воздуха
    31 ** = тест положения направление потока воздуха (абсолютный)
    32 ** = тест положения направление потока воздуха (относительный)(с учетом ограничений)
    33 ** = тест положения направление потока воздуха в мкс
    34 *** = установка максимального положения направление потока воздуха

    // 4** - связан с датчиками температуры
    41 = тест датчика температуры(запрос температуры)
    42 ** = установить минимальную начальную температуру
    43 ** = установить максимальную начальную температуру
    44 ** = установить разницу скорости вентилятора от заданой температуры
    45 ** = установить температуру в печке
    46 ** = установить температуру в машине
    47 ** = установить температуру вне машины

    // 5** - прочее
    51 ** = время обновления авто режима
    52 * * * * = изменнить PID (0 = servoHotPID; 1 = fanSpeedPID) значение P I D
    53 * = показать PID (0 = servoHotPID; 1 = fanSpeedPID)
    54 * = изменить servoTickCount(max 255)
    55 * = изменить fanSpeedType(max 255)(0 = PID, 1 = Linear)
  }
*/

#define VERSION 2.15 //old 2.14
//Выводы, к которому подключён:
#define TRANSISTOR_BUS 3 //TRANSISTOR
#define ONE_WIRE_BUS 4 //DS18B20 - датчик температуры
#define RELAY_AIR_RECIRCULATION_BUS 6 // Реле рециркуляции воздуха
#define RELAY_AIR_CONDITIONING_BUS 7 // Реле кондиционера
#define RELAY_SERVO_SIGNAL_BUS 8 //RELAY
#define SERVO_BUS 9 //SERVO_MOTOR
#define BUTTON_AIR_RECIRCULATION_BUS 10 // Кнопка рециркуляции
#define BUTTON_AIR_CONDITIONING_BUS 11 // Кнопка кондиционера
//Аналоговые
#define HEATER_TEMP_BUS A0 // A0 = Температура в печке

#define TEMPERATURE_PRECISION 12 // точность измерений (9 ... 12)

//Физические ограничения серво-мотора
#define SERVO_ROTATE_MIN 0
#define SERVO_ROTATE_MAX 180

/*
  0 = автоматический режим
  1 = ручной режим
  2 = сервисный режим
*/
byte mode = 0;
boolean initEnd = false; //Чтобы не дерггать сервомотор в начале

OneWire oneWire(ONE_WIRE_BUS); //Говорим к какому пину подключены датчики температуры
DallasTemperature sensor(&oneWire);
DeviceAddress addressTempInHeater = {0x28, 0xBC, 0x67, 0x94, 0x97, 0x02, 0x03, 0xEA};
DeviceAddress addressTempInCar = {0x28, 0xAA, 0x19, 0xC8, 0x52, 0x14, 0x01, 0x9A};

ServoSmooth servoHot; //контроль заслонки тепло-хoлод
GyverPID servoHotPID(14, 0.82, 0); //15.2 0.82 0
GyverPID fanSpeedPID(15.2, 0.82, 0);

float tempNowHeater = 0.0; // текущее значение температуры в печке
float tempNowCar = 0.0; // текущее значение температуры в машине
float tempNowOutCar = 0.0; // текущее значение температуры на улице

int speedFan = 0; // скорость вентилятора

int rotateServoHot = -1; // положение заслонки тепло-холод

struct {
  int AUTO_MODE_UPDATE_TIME = 2000; // время обновления авто-режима
  
  int tempMinStartWork = 40; // минимальная температура при которой начнётся нагрев салона
  int tempMaxStartWork = 20; // tempMinStartWork + tempMaxStartWork = когда вентелятор будет дуть на полную
  int diffSpeedFan = 6; // от 0 до diffSpeedFan, интерпритируются от minSpeedFan до 255, разница скорости вентилятора от заданной температуры

  float tempSet = 22.0; // установление значения желаемой температуры

  boolean alwaysOnFan = true;
  int minSpeedFan = 120;

  int maxRotateServoHot = 180;
  float deadRotateServoHot = 1; // разница температуры от заданной при которой не будет работать заслонка тепло-холод

  //int maxRotateServoAirWay = 180; // пока не актуально, для заслонки направления воздуха

  float servoHotPID[3] = { 14, 0.82, 0 };
  float fanSpeedPID[3] = { 15.2, 0.82, 0 };

  byte servoTickCount = 5;
  byte fanSpeedType = 1;
} setting;

void updatePID(){
  //servoHotPID loading
  servoHotPID.Kp = setting.servoHotPID[0];
  servoHotPID.Ki = setting.servoHotPID[1];
  servoHotPID.Kd = setting.servoHotPID[2];
  //fanSpeedPID loading
  fanSpeedPID.Kp = setting.fanSpeedPID[0];
  fanSpeedPID.Ki = setting.fanSpeedPID[1];
  fanSpeedPID.Kd = setting.fanSpeedPID[2];
}

void setup() {
  TCCR2B = 0b00000001; // x1
  TCCR2A = 0b00000011; // fast pwm

  pinMode(RELAY_SERVO_SIGNAL_BUS, OUTPUT);
  digitalWrite(RELAY_SERVO_SIGNAL_BUS, LOW);

  Serial.begin(9600);
  load();
  initControllButton();
  initSensorTemperature();
  initControllServo();

  servoHotPID.setDt(setting.AUTO_MODE_UPDATE_TIME * setting.servoTickCount);
  servoHotPID.setpoint = setting.tempSet;
  servoHotPID.setLimits(SERVO_ROTATE_MIN, SERVO_ROTATE_MAX);
  servoHotPID.setDirection(REVERSE);

  fanSpeedPID.setDt(setting.AUTO_MODE_UPDATE_TIME);
  fanSpeedPID.setpoint = setting.tempSet;
  fanSpeedPID.setLimits(setting.minSpeedFan, 255);
  fanSpeedPID.setDirection(NORMAL);
}

void loop() {
  if (Serial.available()) {
    int command = Serial.parseInt();
    switch (command) {
      case 403:
        getVersion();
        break;
      case 404:
        save();
        break;
      case 405:
        resetSave();
        break;
      case 908:
        getStatus();
        break;
      case 909: mode = 0;
        Serial.println("Auto_Mode");
        break;
      case 910: mode = 1;
        Serial.println("Manual_Mode");
        break;
      case 911: mode = 2;
        Serial.println("Service_Mode");
        break;
      default: switch (mode) {
          case 0:
            serialAutoMode(command);
            break;
          case 1:
            serialManualMode(command);
            break;
          case 2:
            serialServiceMode(command);
            break;
        }
        break;
    }
  }
  switch (mode) {
    case 0:
      autoMode();
      break;
    case 1:
      manualMode();
      break;
    case 2:
      serviceMode();
      break;
  }

  servoHot.tick();

  //Чтобы не дергать серво-мотор тепло-холод при включении
  if(!initEnd){
    static uint32_t ztmr;
    if (millis() - ztmr < 4000) return;
    ztmr = millis();
    digitalWrite(RELAY_SERVO_SIGNAL_BUS, HIGH);

    rotateServoHot = -1;
    
    initEnd = true;
  }
}

void getStatus(){
  Serial.print("mode ");
  Serial.println(mode);
  
  Serial.print("AUTO_MODE_UPDATE_TIME ");
  Serial.println(setting.AUTO_MODE_UPDATE_TIME);

  Serial.print("tempMinStartWork ");
  Serial.println(setting.tempMinStartWork);

  Serial.print("tempMaxStartWork ");
  Serial.println(setting.tempMaxStartWork);

  Serial.print("diffSpeedFan ");
  Serial.println(setting.diffSpeedFan);
  
  Serial.print("Temp_Set: ");
  Serial.println(setting.tempSet);

  Serial.print("AlwaysOnFan ");
  Serial.println(setting.alwaysOnFan);

  Serial.print("minSpeedFan ");
  Serial.println(setting.minSpeedFan);

  Serial.print("maxRotateServoHot ");
  Serial.println(setting.maxRotateServoHot);

  Serial.print("deadRotateServoHot ");
  Serial.println(setting.deadRotateServoHot);
/*
  Serial.print("maxRotateServoAirWay ");
  Serial.println(setting.maxRotateServoAirWay);*/

  showServoHotPID();
  showFanSpeedPID();

  Serial.print("servoTickCount ");
  Serial.println(setting.servoTickCount);

  Serial.print("fanSpeedType ");
  Serial.println(setting.fanSpeedType);

  getStatusControllFan();
  getStatusControllServo();
  getTemp();
  getVersion();

  switch(mode){
    case 0:
      getStatusModeAuto();
      break;
    case 1:
      getStatusModeManual();
      break;
    case 2:
      getStatusModeService();
      break;
  }

  getStatusControllFan();
  getStatusControllButton();
}

void getVersion(){
  Serial.print("VERSION: ");
  Serial.println(VERSION);
}
