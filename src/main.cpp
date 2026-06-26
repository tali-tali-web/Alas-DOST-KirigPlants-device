#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads;

// ---------------- CONFIG ----------------
const char* WIFI_SSID = "ESP_TEST";
const char* WIFI_PASS = "12345678";
const char* API_URL = "http://10.42.0.1:8000/api/upload";

// ---------------- I2C ----------------
#define SDA_PIN 20
#define SCL_PIN 21

// ---------------- BUFFER ----------------
const int BATCH_SIZE = 128;

volatile int16_t samples[BATCH_SIZE];
volatile int sampleIndex = 0;

SemaphoreHandle_t bufferMutex;

// ---------------- STATS ----------------
volatile uint32_t totalSamples = 0;
uint32_t lastRatePrint = 0;
uint32_t lastSampleCount = 0;

// ---------------- WIFI ----------------
void connectWiFi()
{
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi connected");
}

// ---------------- UPLOAD TASK ----------------
void uploadTask(void *pv)
{
    int16_t uploadBuffer[BATCH_SIZE];

    while (true)
    {
        bool batchReady = false;

        if (xSemaphoreTake(bufferMutex, portMAX_DELAY))
        {
            if (sampleIndex >= BATCH_SIZE)
            {
                for (int i = 0; i < BATCH_SIZE; i++)
                {
                    uploadBuffer[i] = samples[i];
                }

                sampleIndex = 0;
                batchReady = true;
            }

            xSemaphoreGive(bufferMutex);
        }

        if (batchReady && WiFi.status() == WL_CONNECTED)
        {
            String json = "{";
            json += "\"esp_chip_id\":\"000011112222\",";
            json += "\"duration\":4.0,";
            json += "\"samples\":[";

            for (int i = 0; i < BATCH_SIZE; i++)
            {
                json += String(uploadBuffer[i]);

                if (i < BATCH_SIZE - 1)
                {
                    json += ",";
                }
            }

            json += "]}";

            HTTPClient http;

            http.begin(API_URL);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("api-key", "something-stupid-over-here");

            int code = http.POST(json);

            Serial.print("HTTP: ");
            Serial.println(code);

            if (code > 0)
            {
                Serial.println(http.getString());
            }
            else
            {
                Serial.print("Error: ");
                Serial.println(http.errorToString(code));
            }
            
            http.end();
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ---------------- SAMPLING TASK ----------------
void sampleTask(void *pv)
{
    while (true)
    {
        int16_t val = ads.readADC_Differential_0_1();

        if (xSemaphoreTake(bufferMutex, portMAX_DELAY))
        {
            if (sampleIndex < BATCH_SIZE)
            {
                samples[sampleIndex++] = val;
                totalSamples++;
            }

            xSemaphoreGive(bufferMutex);
        }

        // Tune this if needed:
        // 20 ms = faster
        // 24 ms = slower
        vTaskDelay(pdMS_TO_TICKS(22));
    }
}

// ---------------- SETUP ----------------
void setup()
{
    Serial.begin(115200);
    delay(1500);

    Wire.begin(SDA_PIN, SCL_PIN);

    connectWiFi();

    if (!ads.begin())
    {
        Serial.println("ADS FAIL");

        while (true)
        {
            delay(1000);
        }
    }

    ads.setGain(GAIN_SIXTEEN);
    ads.setDataRate(RATE_ADS1115_128SPS);

    bufferMutex = xSemaphoreCreateMutex();

    xTaskCreate(    p
        sampleTask,
        "SampleTask",
        4096,
        NULL,
        2,
        NULL
    );

    xTaskCreate(
        uploadTask,
        "UploadTask",
        4096,
        NULL,
        1,
        NULL
    );

    Serial.println("System ready");
}

// ---------------- LOOP ----------------
void loop()
{
    uint32_t now = millis();

    if (now - lastRatePrint >= 10000)
    {
        uint32_t diff = totalSamples - lastSampleCount;

        Serial.print("Samples: ");
        Serial.print(totalSamples);

        Serial.print(" | SPS: ");
        Serial.println(diff / 10.0);

        lastSampleCount = totalSamples;
        lastRatePrint = now;
    }

    delay(100);
}