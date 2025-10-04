#include <SPFD5408_Adafruit_GFX.h>
#include <SPFD5408_Adafruit_TFTLCD.h>
#include <SPFD5408_TouchScreen.h>
unsigned long startTime = 0;
int pulseCount = 0;
int lastSensorValue = 0;
bool lastAboveThreshold = false;
const unsigned long interval = 2000; // 5 секунд
int threshold = 10; // Порог для определения пульса
// Пины дисплея
#define LCD_CS A3
#define LCD_CD A2
#define LCD_WR A1
#define LCD_RD A0
#define LCD_RESET A4

// Пины сенсорного экрана
#define YP A1
#define XM A2
#define YM 7
#define XP 6

// Калибровочные значения для сенсорного экрана
#define TS_MINX 178
#define TS_MINY 75
#define TS_MAXX 931
#define TS_MAXY 895

// Цвета
#define BLACK   0x0000
#define WHITE   0xFFFF
#define GREEN   0x07E0

// Объект дисплея
Adafruit_TFTLCD tft(LCD_CS, LCD_CD, LCD_WR, LCD_RD, LCD_RESET);
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

// Размер экрана
const int screenWidth = 240;
const int screenHeight = 320;
const int delta = 50;
// Параметры графика ЭКГ
int xPos = 0; // текущая позиция по X
int prevY = screenHeight / 2; // предыдущая Y (начинаем по центру)
void drawThickLine(int x1, int y1, int x2, int y2, uint16_t color, int thickness);
void setup() {
  Serial.begin(9600);
  
  pinMode(1, INPUT);
  
  tft.reset();
  tft.begin(0x9341);
  tft.setRotation(1); // горизонтальная ориентация
  tft.fillScreen(BLACK);
  
  // Нарисовать ось или линию центра
  tft.drawLine(0, screenHeight/2, screenWidth, screenHeight/2, WHITE);
}
void loop() {
  int sensorValue = analogRead(A0);
  int y = map(sensorValue, 0, 1023, (screenHeight/2 + delta), (screenHeight/2 - delta));
  
  // Обнаружение удара (пульса)
  bool currentAboveThreshold = sensorValue > threshold;
  if (currentAboveThreshold && !lastAboveThreshold) {
    pulseCount++;
  }
  lastAboveThreshold = currentAboveThreshold;

  unsigned long currentTime = millis();

  if (currentTime - startTime >= interval) {
    // Отобразить количество пульсов за 5 секунд
    tft.fillRect(0, 0, 120, 20, BLACK);
    tft.setCursor(10, 10);
    tft.setTextColor(WHITE);
    tft.setTextSize(2);
    tft.print("HR:");
    tft.print(pulseCount);
    pulseCount = 0;
    startTime = currentTime;
  }

  // Обновление графика
  int y2 = map(sensorValue, 0, 1023, (screenHeight/0.6 + delta), (screenHeight/2 - delta));
  if (xPos >= screenWidth) {
    tft.fillRect(0, 0, screenWidth, screenHeight, BLACK);
    tft.drawLine(0, screenHeight/2, screenWidth, screenHeight/2, WHITE);
    xPos = 0;
  }

  int thickness = 1;
  drawThickLine(xPos - 1, prevY, xPos, y2, GREEN, thickness);
  prevY = y2;
  xPos++;

  delay(20);
}

void drawThickLine(int x1, int y1, int x2, int y2, uint16_t color, int thickness) {
  if (thickness < 1) {
    thickness = 1; // минимальная толщина
  }
  
  // Рисуем линию с границей для контраста
 // for (int i = -thickness / 2; i <= thickness / 2; i++) {
   // tft.drawLine(x1, y1 + i, x2, y2 + i, borderColor);
  //}

  // Рисуем основную линию чуть уже внутри границы
  for (int i = -thickness / 4; i <= thickness / 4; i++) {
    tft.drawLine(x1, y1 + i, x2, y2 + i, color);
  }
}
