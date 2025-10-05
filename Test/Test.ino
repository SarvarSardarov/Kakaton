#include <SPFD5408_Adafruit_GFX.h>
#include <SPFD5408_Adafruit_TFTLCD.h>
#include <SPFD5408_TouchScreen.h>

// Пины дисплея
#define LCD_CS A3
#define LCD_CD A2
#define LCD_WR A1
#define LCD_RD A0
#define LCD_RESET A4

// Цвета
#define BLACK   0x0000
#define WHITE   0xFFFF
#define GREEN   0x07E0
#define RED     0xF800
#define BLUE    0x001F
#define GRAY    0x2104
#define YELLOW  0xFFE0

Adafruit_TFTLCD tft(LCD_CS, LCD_CD, LCD_WR, LCD_RD, LCD_RESET);

const int screenWidth = 320;
const int screenHeight = 240;

// Параметры графика
int xPos = 0;
int prevY = screenHeight / 2;
int rawValue = 0;
int filteredValue = 0;

// Настройки для AD8232
int ECG_OFFSET = 915;
int ECG_GAIN = 17;

// Простой фильтр скользящего среднего
const int NUM_SAMPLES = 7;
int samples[NUM_SAMPLES];
int sampleIndex = 0;

// Для отображения параметров
unsigned long lastParamUpdate = 0;
const unsigned long PARAM_UPDATE_INTERVAL = 1000;

// Переменные для хранения предыдущих значений
String lastRawValue = "";
String lastFilteredValue = "";
String lastOffsetValue = "";
String lastGainValue = "";
String lastBPMValue = "";

// СИСТЕМА ПОДСЧЕТА ПУЛЬСА
const int PEAK_BUFFER_SIZE = 10; // Буфер для 10 последних пиков
unsigned long peakTimes[PEAK_BUFFER_SIZE]; // Время обнаружения пиков
int peakBufferIndex = 0;
bool peakBufferFull = false;

unsigned long lastPeakTime = 0;
const int PEAK_THRESHOLD = 100; // Порог для обнаружения пика
const unsigned long PEAK_TIMEOUT = 300; // Минимальное время между пиками (мс)
bool wasAboveThreshold = false;
int currentBPM = 0;

void setup() {
    Serial.begin(115200);
    
    // Инициализация фильтра
    for (int i = 0; i < NUM_SAMPLES; i++) {
        samples[i] = analogRead(A0);
    }
    
    // Инициализация буфера пиков
    for (int i = 0; i < PEAK_BUFFER_SIZE; i++) {
        peakTimes[i] = 0;
    }
    
    tft.reset();
    tft.begin(0x9341);
    tft.setRotation(1);
    tft.fillScreen(BLACK);
    
    drawStaticElements();
    drawGrid();
    
    Serial.println("=== ECG AD8232 HEART RATE MONITOR ===");
    Serial.println("Timestamp,RawValue,FilteredValue,BPM,PeakDetected");
    Serial.println("START_DATA_CSV");
}

void drawStaticElements() {
    // Заголовок
    tft.setCursor(10, 5);
    tft.setTextColor(GREEN);
    tft.setTextSize(2);
    tft.print("ECG Heart Rate");
    
    // Статические надписи параметров
    tft.setCursor(10, 30);
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.print("Offset: ");
    
    tft.setCursor(60, 30);
    tft.print(ECG_OFFSET);
    lastOffsetValue = String(ECG_OFFSET);
    
    tft.setCursor(100, 30);
    tft.print("Gain: ");
    
    tft.setCursor(140, 30);
    tft.print(ECG_GAIN);
    tft.print("x");
    lastGainValue = String(ECG_GAIN) + "x";
    
    // Статические надписи для данных
    tft.setCursor(180, 30);
    tft.print("BPM: ");
    
    tft.setCursor(220, 30);
    tft.print("---");
    
    tft.setCursor(260, 30);
    tft.print("Raw: ");
}

void drawGrid() {
    for (int y = 50; y < screenHeight; y += 20) {
        tft.drawLine(0, y, screenWidth, y, GRAY);
    }
    for (int x = 0; x < screenWidth; x += 20) {
        tft.drawLine(x, 50, x, screenHeight, GRAY);
    }
    tft.drawLine(0, screenHeight/2, screenWidth, screenHeight/2, BLUE);
}

void updateParameters() {
    // Обновляем Raw значение
    String currentRaw = String(rawValue);
    if (currentRaw != lastRawValue) {
        tft.fillRect(290, 30, 30, 8, BLACK);
        tft.setCursor(290, 30);
        tft.setTextColor(WHITE);
        tft.setTextSize(1);
        tft.print(currentRaw);
        lastRawValue = currentRaw;
    }
    
    // Обновляем BPM значение
    String currentBPMStr = String(currentBPM);
    if (currentBPMStr != lastBPMValue) {
        tft.fillRect(220, 30, 30, 8, BLACK);
        tft.setCursor(220, 30);
        tft.setTextColor(RED);
        tft.setTextSize(1);
        tft.print(currentBPMStr);
        lastBPMValue = currentBPMStr;
    }

// Обновляем Offset если изменился
    String currentOffset = String(ECG_OFFSET);
    if (currentOffset != lastOffsetValue) {
        tft.fillRect(60, 30, 30, 8, BLACK);
        tft.setCursor(60, 30);
        tft.setTextColor(WHITE);
        tft.setTextSize(1);
        tft.print(currentOffset);
        lastOffsetValue = currentOffset;
    }
    
    // Обновляем Gain если изменился
    String currentGain = String(ECG_GAIN) + "x";
    if (currentGain != lastGainValue) {
        tft.fillRect(140, 30, 30, 8, BLACK);
        tft.setCursor(140, 30);
        tft.setTextColor(WHITE);
        tft.setTextSize(1);
        tft.print(currentGain);
        lastGainValue = currentGain;
    }
}

bool detectPeak() {
    bool peakDetected = false;
    bool isAboveThreshold = filteredValue > PEAK_THRESHOLD;
    
    // Обнаружение фронта: было ниже порога, стало выше
    if (isAboveThreshold && !wasAboveThreshold) {
        unsigned long currentTime = millis();
        
        // Проверяем, что прошло достаточно времени с последнего пика
        if (currentTime - lastPeakTime > PEAK_TIMEOUT) {
            peakDetected = true;
            lastPeakTime = currentTime;
            
            // Добавляем время пика в буфер
            peakTimes[peakBufferIndex] = currentTime;
            peakBufferIndex = (peakBufferIndex + 1) % PEAK_BUFFER_SIZE;
            if (peakBufferIndex == 0) peakBufferFull = true;
            
            // Подсчитываем BPM
            calculateBPM();
        }
    }
    
    wasAboveThreshold = isAboveThreshold;
    return peakDetected;
}

void calculateBPM() {
    if (!peakBufferFull && peakBufferIndex < 2) {
        return; // Недостаточно данных
    }
    
    int numPeaksToUse = peakBufferFull ? PEAK_BUFFER_SIZE : peakBufferIndex;
    if (numPeaksToUse < 2) return;
    
    long totalInterval = 0;
    int validIntervals = 0;
    
    // Вычисляем средний интервал между пиками
    for (int i = 1; i < numPeaksToUse; i++) {
        int currentIndex = (peakBufferIndex - i + PEAK_BUFFER_SIZE) % PEAK_BUFFER_SIZE;
        int prevIndex = (peakBufferIndex - i - 1 + PEAK_BUFFER_SIZE) % PEAK_BUFFER_SIZE;
        
        long interval = peakTimes[currentIndex] - peakTimes[prevIndex];
        
        // Фильтруем нереальные интервалы (пульс от 30 до 200 уд/мин)
        if (interval > 2000 && interval < 2000) {
            totalInterval += interval;
            validIntervals++;
        }
    }
    
    if (validIntervals > 0) {
        double averageInterval = (double)totalInterval / validIntervals;
        // Переводим мс/удар в удары/минуту: 60000 мс / интервал
        currentBPM = (int)(60000.0 / averageInterval);
        
        // Ограничиваем реалистичные значения
        if (currentBPM < 30) currentBPM = 0;
        if (currentBPM > 200) currentBPM = 0;
    }
}

int readFilteredADC() {
    samples[sampleIndex] = analogRead(A0);
    sampleIndex = (sampleIndex + 1) % NUM_SAMPLES;
    
    long sum = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        sum += samples[i];
    }
    return sum / NUM_SAMPLES;
}

void loop() {
    // Чтение и фильтрация
    rawValue = readFilteredADC();
    
    // Смещение и усиление для AD8232
    int centeredValue = rawValue - ECG_OFFSET;
    filteredValue = centeredValue * ECG_GAIN;
    filteredValue = constrain(filteredValue, -500, 500);
    
    // Обнаружение пиков пульса
    bool peakDetected = detectPeak();
    
    // Вывод в Serial
    Serial.print(millis());
    Serial.print(",");
    Serial.print(rawValue);
    Serial.print(",");
    Serial.print(filteredValue);
    Serial.print(",");
    Serial.print(currentBPM);
    Serial.print(",");
    Serial.println(peakDetected ? "1" : "0");
    
    // Маппинг для дисплея
    int y = map(filteredValue, -500, 500, screenHeight - 20, 20);
    
    // Прокрутка графика
    if (xPos >= screenWidth) {
        xPos = 0;
        tft.fillRect(0, 50, screenWidth, screenHeight - 50, BLACK);
drawGrid();
    }
    
    // Рисуем линию
    if (xPos > 0) {
        tft.drawLine(xPos - 1, prevY, xPos, y, GREEN);
    }
    
    // Подсвечиваем пики красным
    if (peakDetected) {
        tft.fillCircle(xPos, y, 3, RED);
    }
    
    prevY = y;
    xPos++;
    
    // Обновляем отображение параметров
    if (millis() - lastParamUpdate >= PARAM_UPDATE_INTERVAL) {
        updateParameters();
        lastParamUpdate = millis();
    }
    
    // Изменение параметров через Serial
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command.startsWith("OFFSET:")) {
            int newOffset = command.substring(7).toInt();
            ECG_OFFSET = newOffset;
            Serial.print("Offset changed to: ");
            Serial.println(ECG_OFFSET);
        }
        else if (command.startsWith("GAIN:")) {
            int newGain = command.substring(5).toInt();
            ECG_GAIN = newGain;
            Serial.print("Gain changed to: ");
            Serial.println(ECG_GAIN);
        }
    }
    
    delay(30);
}
#include <SPFD5408_Adafruit_GFX.h>
#include <SPFD5408_Adafruit_TFTLCD.h>
#include <SPFD5408_TouchScreen.h>

// Пины дисплея
#define LCD_CS A3
#define LCD_CD A2
#define LCD_WR A1
#define LCD_RD A0
#define LCD_RESET A4

// Цвета
#define BLACK   0x0000
#define WHITE   0xFFFF
#define GREEN   0x07E0
#define RED     0xF800
#define BLUE    0x001F
#define GRAY    0x2104
#define YELLOW  0xFFE0

Adafruit_TFTLCD tft(LCD_CS, LCD_CD, LCD_WR, LCD_RD, LCD_RESET);

const int screenWidth = 320;
const int screenHeight = 240;

// Параметры графика
int xPos = 0;
int prevY = screenHeight / 2;
int rawValue = 0;
int filteredValue = 0;

// Настройки для AD8232
int ECG_OFFSET = 915;
int ECG_GAIN = 17;

// Простой фильтр скользящего среднего
const int NUM_SAMPLES = 7;
int samples[NUM_SAMPLES];
int sampleIndex = 0;

// Для отображения параметров
unsigned long lastParamUpdate = 0;
const unsigned long PARAM_UPDATE_INTERVAL = 1000;

// Переменные для хранения предыдущих значений
String lastRawValue = "";
String lastFilteredValue = "";
String lastOffsetValue = "";
String lastGainValue = "";
String lastBPMValue = "";

// СИСТЕМА ПОДСЧЕТА ПУЛЬСА
const int PEAK_BUFFER_SIZE = 10; // Буфер для 10 последних пиков
unsigned long peakTimes[PEAK_BUFFER_SIZE]; // Время обнаружения пиков
int peakBufferIndex = 0;
bool peakBufferFull = false;

unsigned long lastPeakTime = 0;
const int PEAK_THRESHOLD = 100; // Порог для обнаружения пика
const unsigned long PEAK_TIMEOUT = 300; // Минимальное время между пиками (мс)
bool wasAboveThreshold = false;
int currentBPM = 0;

void setup() {
    Serial.begin(115200);
    
    // Инициализация фильтра
    for (int i = 0; i < NUM_SAMPLES; i++) {
        samples[i] = analogRead(A0);
    }
    
    // Инициализация буфера пиков
    for (int i = 0; i < PEAK_BUFFER_SIZE; i++) {
        peakTimes[i] = 0;
    }
    
    tft.reset();
    tft.begin(0x9341);
    tft.setRotation(1);
    tft.fillScreen(BLACK);
    
    drawStaticElements();
    drawGrid();
    
    Serial.println("=== ECG AD8232 HEART RATE MONITOR ===");
    Serial.println("Timestamp,RawValue,FilteredValue,BPM,PeakDetected");
    Serial.println("START_DATA_CSV");
}

void drawStaticElements() {
    // Заголовок
    tft.setCursor(10, 5);
    tft.setTextColor(GREEN);
    tft.setTextSize(2);
    tft.print("ECG Heart Rate");
    
    // Статические надписи параметров
    tft.setCursor(10, 30);
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.print("Offset: ");
    
    tft.setCursor(60, 30);
    tft.print(ECG_OFFSET);
    lastOffsetValue = String(ECG_OFFSET);
    
    tft.setCursor(100, 30);
    tft.print("Gain: ");
    
    tft.setCursor(140, 30);
    tft.print(ECG_GAIN);
    tft.print("x");
    lastGainValue = String(ECG_GAIN) + "x";
    
    // Статические надписи для данных
    tft.setCursor(180, 30);
tft.print("BPM: ");
    
    tft.setCursor(220, 30);
    tft.print("---");
    
    tft.setCursor(260, 30);
    tft.print("Raw: ");
}

void drawGrid() {
    for (int y = 50; y < screenHeight; y += 20) {
        tft.drawLine(0, y, screenWidth, y, GRAY);
    }
    for (int x = 0; x < screenWidth; x += 20) {
        tft.drawLine(x, 50, x, screenHeight, GRAY);
    }
    tft.drawLine(0, screenHeight/2, screenWidth, screenHeight/2, BLUE);
}

void updateParameters() {
    // Обновляем Raw значение
    String currentRaw = String(rawValue);
    if (currentRaw != lastRawValue) {
        tft.fillRect(290, 30, 30, 8, BLACK);
        tft.setCursor(290, 30);
        tft.setTextColor(WHITE);
        tft.setTextSize(1);
        tft.print(currentRaw);
        lastRawValue = currentRaw;
    }
    
    // Обновляем BPM значение
    String currentBPMStr = String(currentBPM);
    if (currentBPMStr != lastBPMValue) {
        tft.fillRect(220, 30, 30, 8, BLACK);
        tft.setCursor(220, 30);
        tft.setTextColor(RED);
        tft.setTextSize(1);
        tft.print(currentBPMStr);
        lastBPMValue = currentBPMStr;
    }
    
    // Обновляем Offset если изменился
    String currentOffset = String(ECG_OFFSET);
    if (currentOffset != lastOffsetValue) {
        tft.fillRect(60, 30, 30, 8, BLACK);
        tft.setCursor(60, 30);
        tft.setTextColor(WHITE);
        tft.setTextSize(1);
        tft.print(currentOffset);
        lastOffsetValue = currentOffset;
    }
    
    // Обновляем Gain если изменился
    String currentGain = String(ECG_GAIN) + "x";
    if (currentGain != lastGainValue) {
        tft.fillRect(140, 30, 30, 8, BLACK);
        tft.setCursor(140, 30);
        tft.setTextColor(WHITE);
        tft.setTextSize(1);
        tft.print(currentGain);
        lastGainValue = currentGain;
    }
}

bool detectPeak() {
    bool peakDetected = false;
    bool isAboveThreshold = filteredValue > PEAK_THRESHOLD;
    
    // Обнаружение фронта: было ниже порога, стало выше
    if (isAboveThreshold && !wasAboveThreshold) {
        unsigned long currentTime = millis();
        
        // Проверяем, что прошло достаточно времени с последнего пика
        if (currentTime - lastPeakTime > PEAK_TIMEOUT) {
            peakDetected = true;
            lastPeakTime = currentTime;
            
            // Добавляем время пика в буфер
            peakTimes[peakBufferIndex] = currentTime;
            peakBufferIndex = (peakBufferIndex + 1) % PEAK_BUFFER_SIZE;
            if (peakBufferIndex == 0) peakBufferFull = true;
            
            // Подсчитываем BPM
            calculateBPM();
        }
    }
    
    wasAboveThreshold = isAboveThreshold;
    return peakDetected;
}

void calculateBPM() {
    if (!peakBufferFull && peakBufferIndex < 2) {
        return; // Недостаточно данных
    }
    
    int numPeaksToUse = peakBufferFull ? PEAK_BUFFER_SIZE : peakBufferIndex;
    if (numPeaksToUse < 2) return;
    
    long totalInterval = 0;
    int validIntervals = 0;
    
    // Вычисляем средний интервал между пиками
    for (int i = 1; i < numPeaksToUse; i++) {
        int currentIndex = (peakBufferIndex - i + PEAK_BUFFER_SIZE) % PEAK_BUFFER_SIZE;
        int prevIndex = (peakBufferIndex - i - 1 + PEAK_BUFFER_SIZE) % PEAK_BUFFER_SIZE;
        
        long interval = peakTimes[currentIndex] - peakTimes[prevIndex];
        
        // Фильтруем нереальные интервалы (пульс от 30 до 200 уд/мин)
        if (interval > 2000 && interval < 2000) {
            totalInterval += interval;
            validIntervals++;
        }
    }
    
    if (validIntervals > 0) {
        double averageInterval = (double)totalInterval / validIntervals;
        // Переводим мс/удар в удары/минуту: 60000 мс / интервал
        currentBPM = (int)(60000.0 / averageInterval);
        
        // Ограничиваем реалистичные значения
        if (currentBPM < 30) currentBPM = 0;
        if (currentBPM > 200) currentBPM = 0;
    }
}

intreadFilteredADC() {
    samples[sampleIndex] = analogRead(A0);
    sampleIndex = (sampleIndex + 1) % NUM_SAMPLES;
    
    long sum = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        sum += samples[i];
    }
    return sum / NUM_SAMPLES;
}

void loop() {
    // Чтение и фильтрация
    rawValue = readFilteredADC();
    
    // Смещение и усиление для AD8232
    int centeredValue = rawValue - ECG_OFFSET;
    filteredValue = centeredValue * ECG_GAIN;
    filteredValue = constrain(filteredValue, -500, 500);
    
    // Обнаружение пиков пульса
    bool peakDetected = detectPeak();
    
    // Вывод в Serial
    Serial.print(millis());
    Serial.print(",");
    Serial.print(rawValue);
    Serial.print(",");
    Serial.print(filteredValue);
    Serial.print(",");
    Serial.print(currentBPM);
    Serial.print(",");
    Serial.println(peakDetected ? "1" : "0");
    
    // Маппинг для дисплея
    int y = map(filteredValue, -500, 500, screenHeight - 20, 20);
    
    // Прокрутка графика
    if (xPos >= screenWidth) {
        xPos = 0;
        tft.fillRect(0, 50, screenWidth, screenHeight - 50, BLACK);
        drawGrid();
    }
    
    // Рисуем линию
    if (xPos > 0) {
        tft.drawLine(xPos - 1, prevY, xPos, y, GREEN);
    }
    
    // Подсвечиваем пики красным
    if (peakDetected) {
        tft.fillCircle(xPos, y, 3, RED);
    }
    
    prevY = y;
    xPos++;
    
    // Обновляем отображение параметров
    if (millis() - lastParamUpdate >= PARAM_UPDATE_INTERVAL) {
        updateParameters();
        lastParamUpdate = millis();
    }
    
    // Изменение параметров через Serial
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command.startsWith("OFFSET:")) {
            int newOffset = command.substring(7).toInt();
            ECG_OFFSET = newOffset;
            Serial.print("Offset changed to: ");
            Serial.println(ECG_OFFSET);
        }
        else if (command.startsWith("GAIN:")) {
            int newGain = command.substring(5).toInt();
            ECG_GAIN = newGain;
            Serial.print("Gain changed to: ");
            Serial.println(ECG_GAIN);
        }
    }
    
    delay(30);
}
#include <SPFD5408_Adafruit_GFX.h>
#include <SPFD5408_Adafruit_TFTLCD.h>
#include <SPFD5408_TouchScreen.h>

// Пины дисплея
#define LCD_CS A3
#define LCD_CD A2
#define LCD_WR A1
#define LCD_RD A0
#define LCD_RESET A4

// Цвета
#define BLACK   0x0000
#define WHITE   0xFFFF
#define GREEN   0x07E0
#define RED     0xF800
#define BLUE    0x001F
#define GRAY    0x2104
#define YELLOW  0xFFE0

Adafruit_TFTLCD tft(LCD_CS, LCD_CD, LCD_WR, LCD_RD, LCD_RESET);

const int screenWidth = 320;
const int screenHeight = 240;

// Параметры графика
int xPos = 0;
int prevY = screenHeight / 2;
int rawValue = 0;
int filteredValue = 0;

// Настройки для AD8232
int ECG_OFFSET = 915;
int ECG_GAIN = 17;

// Простой фильтр
const int NUM_SAMPLES = 3;
int samples[NUM_SAMPLES];
int sampleIndex = 0;

// Для отображения параметров
unsigned long lastParamUpdate = 0;
const unsigned long PARAM_UPDATE_INTERVAL = 1000;

// СИСТЕМА ПОДСЧЕТА ПУЛЬСА - УПРОЩЕННАЯ
const int PEAK_THRESHOLD = 80;
unsigned long lastPeakTime = 0;
unsigned long lastPeakInterval = 0;
int currentBPM = 0;
int peakCount = 0;
unsigned long lastBPMCalculation = 0;
bool wasAboveThreshold = false;

void setup() {
    Serial.begin(115200);
    
    // Инициализация фильтра
    for (int i = 0; i < NUM_SAMPLES; i++) {
        samples[i] = analogRead(A0);
    }
    
    tft.reset();
    tft.begin(0x9341);
    tft.setRotation(1);
    tft.fillScreen(BLACK);
    
    drawStaticElements();
    drawGrid();
    
    Serial.println("=== ECG SIMPLE BPM CALCULATION ===");
    Serial.println("Timestamp,RawValue,FilteredValue,BPM,PeakDetected");
}

void drawStaticElements() {
    // Заголовок
    tft.setCursor(10, 5);
    tft.setTextColor(GREEN);
    tft.setTextSize(2);
    tft.print("ECG Heart Rate");
    
    // Статические надписи
    tft.setCursor(10, 30);
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.print("BPM: ");
    
    tft.setCursor(50, 30);
    tft.print("---");
    
    tft.setCursor(100, 30);
    tft.print("Raw: ");
}

void drawGrid() {
    for (int y = 50; y < screenHeight; y += 20) {
        tft.drawLine(0, y, screenWidth, y, GRAY);
    }
    for (int x = 0; x < screenWidth; x += 20) {
        tft.drawLine(x, 50, x, screenHeight, GRAY);
    }
    tft.drawLine(0, screenHeight/2, screenWidth, screenHeight/2, BLUE);
}

void updateBPMDisplay() {
    tft.fillRect(50, 30, 40, 8, BLACK);
    tft.setCursor(50, 30);
    tft.setTextColor(RED);
    tft.setTextSize(1);
    
    if (currentBPM > 0) {
        tft.print(currentBPM);
    } else {
        tft.print("---");
    }
}

bool detectPeak() {
    bool isAboveThreshold = filteredValue > PEAK_THRESHOLD;
    bool newPeak = false;
    
    // Простой детектор: переход снизу вверх через порог
    if (isAboveThreshold && !wasAboveThreshold) {
        unsigned long currentTime = millis();
        
        // Защита от слишком частых срабатываний (не чаще 200 BPM)
        if (currentTime - lastPeakTime > 300) {
            newPeak = true;
            
            // Рассчитываем интервал между пиками
            if (lastPeakTime > 0) {
                lastPeakInterval = currentTime - lastPeakTime;
                
                // Сразу рассчитываем BPM из одного интервала
                int instantBPM = (int)(60000.0 / lastPeakInterval);
                
                // Фильтруем нереальные значения
                if (instantBPM >= 40 && instantBPM <= 180) {
                    currentBPM = instantBPM;
                    Serial.print("Instant BPM: ");
                    Serial.println(currentBPM);
                }
            }
            
            lastPeakTime = currentTime;
            peakCount++;
        }
    }
    
    wasAboveThreshold = isAboveThreshold;
    return newPeak;
}

int readFilteredADC() {
    samples[sampleIndex] = analogRead(A0);
    sampleIndex = (sampleIndex + 1) % NUM_SAMPLES;
    
    long sum = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
sum += samples[i];
    }
    return sum / NUM_SAMPLES;
}

void loop() {
    // Чтение и фильтрация
    rawValue = readFilteredADC();
    
    // Смещение и усиление
    int centeredValue = rawValue - ECG_OFFSET;
    filteredValue = centeredValue * ECG_GAIN;
    filteredValue = constrain(filteredValue, -500, 500);
    
    // Обнаружение пиков
    bool peakDetected = detectPeak();
    
    // Вывод в Serial
    Serial.print(millis());
    Serial.print(",");
    Serial.print(rawValue);
    Serial.print(",");
    Serial.print(filteredValue);
    Serial.print(",");
    Serial.print(currentBPM);
    Serial.print(",");
    Serial.println(peakDetected ? "1" : "0");
    
    // Маппинг для дисплея
    int y = map(filteredValue, -500, 500, screenHeight - 20, 20);
    
    // Прокрутка графика
    if (xPos >= screenWidth) {
        xPos = 0;
        tft.fillRect(0, 50, screenWidth, screenHeight - 50, BLACK);
        drawGrid();
    }
    
    // Рисуем линию
    if (xPos > 0) {
        tft.drawLine(xPos - 1, prevY, xPos, y, GREEN);
    }
    
    // Подсвечиваем пики
    if (peakDetected) {
        tft.fillCircle(xPos, y, 3, RED);
    }
    
    prevY = y;
    xPos++;
    
    // Обновляем отображение BPM
    if (millis() - lastParamUpdate >= PARAM_UPDATE_INTERVAL) {
        updateBPMDisplay();
        
        // Обновляем Raw значение
        tft.fillRect(130, 30, 40, 8, BLACK);
        tft.setCursor(130, 30);
        tft.setTextColor(WHITE);
        tft.setTextSize(1);
        tft.print(rawValue);
        
        lastParamUpdate = millis();
    }
    
    delay(30);
}
