#include "Audio.h"
#include "Arduino.h"
#include "SD.h"
#include "FS.h"
#include "SPI.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <UrlEncode.h>
#include <Adafruit_MPU6050.h>
#include <Wire.h>
#include <math.h>

#include <driver/i2s.h>
#include <SPIFFS.h>

#define I2S_DOUT 17
#define I2S_BCLK 26
#define I2S_LRC 27
#define SD_CS 5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18

#define I2S_WS 25
#define I2S_SD 33
#define I2S_SCK 32
#define I2S_PORT I2S_NUM_0
#define I2S_SAMPLE_RATE (44100)
#define I2S_SAMPLE_BITS (16)
#define I2S_READ_LEN (16 * 1024)
#define RECORD_TIME (5)  //Seconds
#define I2S_CHANNEL_NUM (1)
#define FLASH_RECORD_SIZE (I2S_CHANNEL_NUM * I2S_SAMPLE_RATE * I2S_SAMPLE_BITS / 8 * RECORD_TIME)

#define THRESHOLD 40 /* Greater the value, more the sensitivity */
#define NUM_BYTES_TO_READ_FROM_FILE 1024

//typedef unsigned char byte;
const int headerSize = 44;
unsigned char header[headerSize];
const char filename[] = "/recording.wav";  //increase filename

long file_number = 0;
File file;

static const i2s_port_t i2s_num = I2S_NUM_0;  // i2s port number
Audio audio;

//IMU variables
float fall_factor = 28;  //change this value depending on how sensitive the fall detection should be
float Acc_total;
Adafruit_MPU6050 mpu;

// wifi login
const char *ssid = "xxxxxxxxxxxxx";
const char *password = "xxxxxxxxxxxxx";

//Phone details to send alert
String phoneNumber = "xxxxxxxxxxxxx";
String apiKey = "xxxxxxxxxxxxx";



RTC_DATA_ATTR int bootCount = 0;
int end_loop = 0;

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup caused by touchpad"); break;
    default: Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }
}

void print_wakeup_touchpad(touch_pad_t touchPin){

  switch (touchPin){
    case 2: Serial.println("Touch detected on GPIO 2"); break;
    case 3: Serial.println("Touch detected on GPIO 15"); break;
    default: Serial.println("Wakeup not by touchpad"); break;
  }
}


  /////////////////////////////////////////////////////////////////////////////////////////////
  ////// Speaker //////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////
void playAudio(){
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    SD.begin(SD_CS);
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(21);
    audio.connecttoFS(SD, "/testSound.wav");

    audio.connecttoFS(SD, "/recording.wav");
    Serial.print("preAudio\n");
    long startTime = 0;
    startTime = millis();

    while (millis() - startTime < 5001) {
      audio.loop();
    }
    i2s_driver_uninstall(I2S_PORT);
    Serial.println("Audio finished");
}
  /////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////


void setup(){
  Serial.begin(115200);
  //delay(1000);  //Take some time to open up the Serial Monitor
  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  //Print the wakeup reason for ESP32 and touchpad too
  touch_pad_t touchPin = esp_sleep_get_touchpad_wakeup_status();
  print_wakeup_reason();
  print_wakeup_touchpad(touchPin);

  //Setup sleep wakeup on Touch Pad 2
  touchSleepWakeUpEnable(T2, THRESHOLD);
  touchSleepWakeUpEnable(T3, THRESHOLD);

  IMU_setup();

  if (touchPin == 2)  //Mic loop
  {
    Serial.print("MICROPHONE\n");
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS);
    i2s_adc(NULL);
  }

  if (touchPin == 3)  // Speaker loop
  {
    playAudio();
  }

  if (bootCount = 1) {
    Serial.println("Going to sleep now");
    esp_deep_sleep_start();
  }

  wifi_connect();
  long loop_time = 0;
  loop_time = millis();
  while (end_loop != 1) {
    Acc_total = IMU_loop();
    //Serial.print("Total acc = ");
    Serial.println(Acc_total);
    if (Acc_total > 28) {
      Serial.println("Fall detected");
      sendMessage("ALERT! FALL DETECTED!");  //send message to whatsapp
      end_loop = 1;
      Serial.print(end_loop);
    }
    
    Serial.println("Going to sleep now");
    esp_deep_sleep_start();
    Serial.println("This will never be printed");
  }
} 

  void loop() {
    //This will never be reached
  }

  /////////////////////////////////////////////////////////////////////////////////////////////
  /////// SD card /////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////
  void initSDCard() {
    if (!SD.begin()) {
      Serial.println("Card Mount Failed");
      return;
    }
    uint8_t cardType = SD.cardType();

    if (cardType == CARD_NONE) {
      Serial.println("No SD card attached");
      return;
    }
    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
      Serial.println("MMC");
    } else if (cardType == CARD_SD) {
      Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
      Serial.println("SDHC");
    } else {
      Serial.println("UNKNOWN");
    }
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    if (SD.remove(filename)) {
      Serial.println("File deleted");
    } else {
      Serial.println("Delete failed");
    }

    file = SD.open(filename, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing");
    }
    byte header[headerSize];
    wavHeader(header, FLASH_RECORD_SIZE);

    file.write(header, headerSize);
    //file.close(); // -- CLOSING FILE
  }

  void latestFileSD(fs::FS & fs, const char *dirname) {
    File root = fs.open(dirname);
    File _file = root.openNextFile();

    while (_file) {
      String fileName = _file.name();
      Serial.println(fileName);
      if (fileName.lastIndexOf('_') != -1) {
        int fromUnderscore = fileName.lastIndexOf('_') + 1;
        int untilDot = fileName.lastIndexOf('.');
        String fileId = fileName.substring(fromUnderscore, untilDot);
        Serial.println(fileId);
        file_number = max(file_number, fileId.toInt());  // replace filenumber if fileId is higher
      }
      _file = root.openNextFile();
    }
  }

  void createDir(fs::FS & fs, const char *path) {
    Serial.printf("Creating Dir: %s\n", path);
    if (fs.mkdir(path)) {
      Serial.println("Dir created");
    } else {
      Serial.println("mkdir failed");
    }
  }

  void removeDir(fs::FS & fs, const char *path) {
    Serial.printf("Removing Dir: %s\n", path);
    if (fs.rmdir(path)) {
      Serial.println("Dir removed");
    } else {
      Serial.println("rmdir failed");
    }
  }

  void readFile(fs::FS & fs, const char *path) {
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    if (!file) {
      Serial.println("Failed to open file for reading");
      return;
    }

    Serial.print("Read from file: ");
    while (file.available()) {
      Serial.write(file.read());
    }
    file.close();
  }

  void writeFile(fs::FS & fs, const char *path, const char *message) {
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing");
      return;
    }
    if (file.print(message)) {
      Serial.println("File written");
    } else {
      Serial.println("Write failed");
    }
    file.close();
  }

  void appendFile(fs::FS & fs, const char *path, const char *message) {
    Serial.printf("Appending to file: %s\n", path);

    File file = fs.open(path, FILE_APPEND);
    if (!file) {
      Serial.println("Failed to open file for appending");
      return;
    }
    if (file.print(message)) {
      Serial.println("Message appended");
    } else {
      Serial.println("Append failed");
    }
    file.close();
  }

  void renameFile(fs::FS & fs, const char *path1, const char *path2) {
    Serial.printf("Renaming file %s to %s\n", path1, path2);
    if (fs.rename(path1, path2)) {
      Serial.println("File renamed");
    } else {
      Serial.println("Rename failed");
    }
  }

  void deleteFile(fs::FS & fs, const char *path) {
    Serial.printf("Deleting file: %s\n", path);
    if (fs.remove(path)) {
      Serial.println("File deleted");
    } else {
      Serial.println("Delete failed");
    }
  }

  void testFileIO(fs::FS & fs, const char *path) {
    File file = fs.open(path);
    static uint8_t buf[512];
    size_t len = 0;
    uint32_t start = millis();
    uint32_t end = start;
    if (file) {
      len = file.size();
      size_t flen = len;
      start = millis();
      while (len) {
        size_t toRead = len;
        if (toRead > 512) {
          toRead = 512;
        }
        file.read(buf, toRead);
        len -= toRead;
      }
      end = millis() - start;
      Serial.printf("%u bytes read for %lu ms\n", flen, end);
      file.close();
    } else {
      Serial.println("Failed to open file for reading");
    }


    file = fs.open(path, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing");
      return;
    }

    size_t i;
    start = millis();
    for (i = 0; i < 2048; i++) {
      file.write(buf, 512);
    }
    end = millis() - start;
    Serial.printf("%u bytes written for %lu ms\n", 2048 * 512, end);
    file.close();
  }

  /////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////


  /////////////////////////////////////////////////////////////////////////////////////////////
  //////// WAV HEADER fuction /////////////////////////////////////////////////////////////////
  void wavHeader(unsigned char *header, int wavSize) {
    header[0] = 'R';
    header[1] = 'I';
    header[2] = 'F';
    header[3] = 'F';
    unsigned int fileSize = wavSize + headerSize - 8;
    header[4] = (unsigned char)(fileSize & 0xFF);
    header[5] = (unsigned char)((fileSize >> 8) & 0xFF);
    header[6] = (unsigned char)((fileSize >> 16) & 0xFF);
    header[7] = (unsigned char)((fileSize >> 24) & 0xFF);
    header[8] = 'W';
    header[9] = 'A';
    header[10] = 'V';
    header[11] = 'E';
    header[12] = 'f';
    header[13] = 'm';
    header[14] = 't';
    header[15] = ' ';
    header[16] = 0x10;
    header[17] = 0x00;
    header[18] = 0x00;
    header[19] = 0x00;
    header[20] = 0x01;
    header[21] = 0x00;
    header[22] = 0x01;
    header[23] = 0x00;
    //changed here to record at 44.1khz.
  // header[24] = 0x44;  
  // header[25] = 0xAC; 
  // header[26] = 0x00;
  // header[27] = 0x00;
  // header[28] = 0x88; 
  // header[29] = 0x58; 
  // header[30] = 0x01;  
  //record at 16k
  // header[24] = 0x80;  
  // header[25] = 0x3e; 
  // header[26] = 0x00;
  // header[27] = 0x00;
  // header[28] = 0x00; 
  // header[29] = 0x7d; 
  // header[30] = 0x00;
  // record at 48k
    header[24] = 0x80; 
    header[25] = 0xBB; 
    header[26] = 0x00;
    header[27] = 0x00;
    header[28] = 0x00; 
    header[29] = 0xEE; 
    header[30] = 0x02; 
    header[31] = 0x00;
    header[32] = 0x02;
    header[33] = 0x00;
    header[34] = 0x10;
    header[35] = 0x00;
    header[36] = 'd';
    header[37] = 'a';
    header[38] = 't';
    header[39] = 'a';
    header[40] = (unsigned char)(wavSize & 0xFF);
    header[41] = (unsigned char)((wavSize >> 8) & 0xFF);
    header[42] = (unsigned char)((wavSize >> 16) & 0xFF);
    header[43] = (unsigned char)((wavSize >> 24) & 0xFF);
  }
  /////////////////////////////////////////////////////////////////////////////////////////////

  /////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////// Micorphone //////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////
  void i2sInit() {
    i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = I2S_SAMPLE_RATE,
      .bits_per_sample = i2s_bits_per_sample_t(I2S_SAMPLE_BITS),
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
      .intr_alloc_flags = 0,
      .dma_buf_count = 64,
      .dma_buf_len = 1024,
      .use_apll = 1,
      .tx_desc_auto_clear = true  // added from other file
    };


    const i2s_pin_config_t pin_config = {
      .bck_io_num = I2S_SCK,
      .ws_io_num = I2S_WS,
      .data_out_num = -1,
      .data_in_num = I2S_SD
    };

    Serial.println(" *** Instlling driever *** ");

    i2s_driver_uninstall(I2S_PORT);
    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);

    Serial.println(" *** setting pins *** ");

    i2s_set_pin(I2S_PORT, &pin_config);
  }


  void i2s_adc_data_scale(uint8_t * d_buff, uint8_t * s_buff, uint32_t len) {
    uint32_t j = 0;
    uint32_t dac_value = 0;
    for (int i = 0; i < len; i += 2) {
      dac_value = ((((uint16_t)(s_buff[i + 1] & 0xf) << 8) | ((s_buff[i + 0]))));
      d_buff[j++] = 0;
      d_buff[j++] = dac_value * 256 / 2048;
    }
  }

  void i2s_adc(void *arg) {

    int i2s_read_len = I2S_READ_LEN;
    int flash_wr_size = 0;
    size_t bytes_read;

    initSDCard();

    Serial.println(" *** Initialising *** ");

    i2sInit();

    Serial.println(" *** Finished Initialising *** ");


    char *i2s_read_buff = (char *)calloc(i2s_read_len, sizeof(char));
    uint8_t *flash_write_buff = (uint8_t *)calloc(i2s_read_len, sizeof(char));

    i2s_read(I2S_PORT, (void *)i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
    i2s_read(I2S_PORT, (void *)i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);

    Serial.println(" *** Recording Start *** ");
    while (flash_wr_size < FLASH_RECORD_SIZE) {
      //read data from I2S bus, in this case, from ADC.
      i2s_read(I2S_PORT, (void *)i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
      i2s_adc_data_scale(flash_write_buff, (uint8_t *)i2s_read_buff, i2s_read_len);
      file.write((const byte *)flash_write_buff, i2s_read_len);
      flash_wr_size += i2s_read_len;
      ets_printf("Sound recording %u%%\n", flash_wr_size * 100 / FLASH_RECORD_SIZE);
      ets_printf("Never Used Stack Size: %u\n", uxTaskGetStackHighWaterMark(NULL));
    }
    Serial.println("closing file");
    file.close();

    free(i2s_read_buff);
    i2s_read_buff = NULL;
    free(flash_write_buff);
    flash_write_buff = NULL;
    i2s_driver_uninstall(I2S_PORT);

    //vTaskDelete(NULL);
  }
  /////////////////////////////////////////////////////////////////////////////////////////////


  /////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////// Send Message function ///////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////
  void sendMessage(String message) {

    // Data to send with HTTP POST
    String url = "https://api.callmebot.com/whatsapp.php?phone=" + phoneNumber + "&apikey=" + apiKey + "&text=" + urlEncode(message);
    HTTPClient http;
    http.begin(url);

    // Specify content-type header
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Send HTTP POST request
    int httpResponseCode = http.POST(url);
    if (httpResponseCode == 200) {
      Serial.print("Message sent successfully\n");
    } else {
      Serial.println("Error sending the message\n");
      Serial.print("HTTP response code: ");
      Serial.println(httpResponseCode);
    }

    // Free resources
    http.end();
  }
  /////////////////////////////////////////////////////////////////////////////////////////////

  /////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////// wifi connect ////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////
  void wifi_connect() {
    WiFi.begin(ssid, password);
    Serial.println("Connecting");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to WiFi network with IP Address: ");
    Serial.println(WiFi.localIP());

    // Send Message to WhatsAPP
    sendMessage("Rememberall connected \n");
  }
  /////////////////////////////////////////////////////////////////////////////////////////////

  /////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////// IMU code ////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////
  void IMU_setup() {
    if (!mpu.begin()) {
      Serial.println("Failed to find MPU6050 chip");
      while (1) {
        delay(10);
      }
    }
    Serial.println("MPU6050 Found!");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  }

  float IMU_loop() {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float Acc_x = a.acceleration.x;
    float Acc_y = a.acceleration.y;
    float Acc_z = a.acceleration.z;

    //calibration
    Acc_x = abs(Acc_x - 0.49);
    Acc_y = abs(Acc_y - 0.26);
    Acc_z = abs(Acc_z - 9.64);

    //squared values
    float x_square = Acc_x * Acc_x;
    float y_square = Acc_y * Acc_y;
    float z_square = Acc_z * Acc_z;

    //sumation and quadriture
    float Acc_total = sqrt(x_square + y_square + z_square);

    return (Acc_total);
  }
  /////////////////////////////////////////////////////////////////////////////////////////////