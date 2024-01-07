#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include "FS.h"               
#include "SD_MMC.h" 
#include "driver/rtc_io.h"
#include <EEPROM.h>   
#include <Wire.h>
#include <ThingSpeak.h>

#define CAMERA_MODEL_AI_THINKER // Has PSRAM

//alamat dan password wifi kalian
const char* ssid = "Kost.323";
const char* password = "Kerjowoii";

//api tele
String BOTtoken = "6682783790:AAGuxnOaG_J-BinPkRvej8V8iMFykamzy5g"; // token dari botfather tele
String chatId = "1329740852"; //id chat dari id bot

//informasi thinkspeak
char* writeApi = "XTKFCFYC2DL01YQJ";
char* readApi = "X9H8P3MMETIUOO4L";
const long channelId = 2372264;

bool sendPhoto = false;

WiFiClientSecure clientTCP;
WiFiClient client;

UniversalTelegramBot bot(BOTtoken, clientTCP);

RTC_DATA_ATTR int bootCount = 0;

void startCameraServer();
void setupLedFlash(int pin);

#define EEPROM_SIZE 1

//Port bawaan untuk CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

//definisi pin 4 sebagai pin lampu flash
#define FLASHpin 4

bool flash = LOW;
bool adaGerakan = false;
int botRequestDelay = 1000;   // setiap 1 detik akan check bot
long lastTimeBotRan; 
int pictureNumber = 0;
const int pirSensor = 13;

void kirimPesanKeBotTele(int pesanBaru);
String kirimFotoKeBotTele();

static void IRAM_ATTR deteksiGerakan(void * arg){
  //Serial.println("ADA GERAKAN!!!");
  adaGerakan = true;
}


void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);
  ThingSpeak.begin(client);
  pinMode(pirSensor, INPUT);
  pinMode(FLASHpin, OUTPUT);
  digitalWrite(FLASHpin, flash);
  
  WiFi.mode(WIFI_STA);
  Serial.println();
  Serial.print("menghubungkan wifi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  clientTCP.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println("WiFi Terhubung");
  startCameraServer();
  Serial.print("ESP32-CAM IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.setDebugOutput(true);
  Serial.println();

  Serial.println("Menghubungkan SD Card");
 
  delay(500);
  if(!SD_MMC.begin()){
    Serial.println("SD Card gagal di hubungkan");
    // return;
  }
 
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("Tidak ada SD Card terdeteksi");
    return;
  }

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  
 
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;  //0-63 lower number means higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;  //0-63 lower number means higher quality
    config.fb_count = 1;
  }
  
  #if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13 , INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
  #endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Gagal mengakses kamera dengan error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  // Drop down frame size for higher initial frame rate
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_CIF);  // UXGA|SXGA|XGA|SVGA|VGA|CIF|QVGA|HQVGA|QQVGA
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);//flip it back
    s->set_brightness(s, 1);//up the blightness just a bit
    s->set_saturation(s, -2);//lower the saturation
  }
  //drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_QVGA);

  #if defined(CAMERA_MODEL_M5STACK_WIDE)
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
  #endif
  // PIR Motion Sensor mode INPUT_PULLUP
  //err = gpio_install_isr_service(0); 
  err = gpio_isr_handler_add(GPIO_NUM_13, &deteksiGerakan, (void *) pirSensor);  // pin sensor yg di gunakan
  if (err != ESP_OK){
    Serial.printf("handler add failed with error 0x%x \r\n", err); 
  }
  err = gpio_set_intr_type(GPIO_NUM_13, GPIO_INTR_POSEDGE);
  if (err != ESP_OK){
    Serial.printf("set intr type failed with error 0x%x \r\n", err);
  }
}

void loop() {
    int pirValue = digitalRead(pirSensor);
    // Do nothing. Everything is done in another task by the web server
    if(sendPhoto){
      Serial.println("persiapan foto");
      kirimFotoKeBotTele(); 
      bot.sendMessage(chatId, "Tidak Ada Wajah Terdeteksi");
      sendPhoto = false; 
    }

    if(adaGerakan == true ){
      bot.sendMessage(chatId, "ADA GERAKAN !!!", "");
      Serial.println("Ada gerakan");
      kirimFotoKeBotTele(); 
      adaGerakan = false;
    }

    ThingSpeak.writeField(channelId, 1, pirValue, writeApi);

    if (millis() > lastTimeBotRan + botRequestDelay){
      int pesanBaru = bot.getUpdates(bot.last_message_received + 1);
      while (pesanBaru){
        Serial.println("got response");
        kirimPesanKeBotTele(pesanBaru);
        pesanBaru = bot.getUpdates(bot.last_message_received + 1);
      }
      lastTimeBotRan = millis();
    }
}

String kirimFotoKeBotTele(){
  const char* myDomain = "api.telegram.org";
  String getAll = "";
  String getBody = "";

  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("kamera gagal ambil gambar");
    delay(1000);
    ESP.restart();
    return "kamera gagal ambil gambar";
  } 

  EEPROM.begin(EEPROM_SIZE);
  pictureNumber = EEPROM.read(0) + 1;
  
  // Path where new picture will be saved in SD Card
  String path = "/gambar" + String(pictureNumber) +".jpg";
  
  fs::FS &fs = SD_MMC;
  Serial.printf("nama gambar: %s\n", path.c_str());
  
  File file = fs.open(path.c_str(), FILE_WRITE);
  if(!file){
    Serial.println("Gagal membuka file");
  }
  else {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("File tersimpan di: %s\n", path.c_str());
    EEPROM.write(0, pictureNumber);
    EEPROM.commit();
  }
  file.close(); 
  
  Serial.println("Menghubungkan " + String(myDomain));

  if (clientTCP.connect(myDomain, 443)) {
    Serial.println("berhasil terhubung");
    
    String head = "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + chatId + "\r\n--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--RandomNerdTutorials--\r\n";

    uint16_t imageLen = fb->len;
    uint16_t extraLen = head.length() + tail.length();
    uint16_t totalLen = imageLen + extraLen;
  
    clientTCP.println("POST /bot"+BOTtoken+"/sendPhoto HTTP/1.1");
    clientTCP.println("Host: " + String(myDomain));
    clientTCP.println("Content-Length: " + String(totalLen));
    clientTCP.println("Content-Type: multipart/form-data; boundary=RandomNerdTutorials");
    clientTCP.println();
    clientTCP.print(head);
  
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n=0;n<fbLen;n=n+1024) {
      if (n+1024<fbLen) {
        clientTCP.write(fbBuf, 1024);
        fbBuf += 1024;
      }
      else if (fbLen%1024>0) {
        size_t remainder = fbLen%1024;
        clientTCP.write(fbBuf, remainder);
      }
    }  
    
    clientTCP.print(tail);    
    esp_camera_fb_return(fb);

    int waitTime = 10000;   // timeout 10 seconds
    long startTimer = millis();
    boolean state = false;
    
    while ((startTimer + waitTime) > millis()){
      Serial.print(".");
      delay(100);      
      while (clientTCP.available()) {
        char c = clientTCP.read();
        if (state==true) getBody += String(c);        
        if (c == '\n') {
          if (getAll.length()==0) state=true; 
          getAll = "";
        } 
        else if (c != '\r')
          getAll += String(c);
        startTimer = millis();
      }
      if (getBody.length()>0) break;
    }

    clientTCP.stop();
    Serial.println(getBody);
  }
  else {
    getBody="Menghubungkan ke api.telegram.org failed.";
    Serial.println("Menghubungkan ke api.telegram.org failed.");
  }
  return getBody;
}

void kirimPesanKeBotTele(int pesanBaru){
  Serial.print("Pesan baru: ");
  Serial.println(pesanBaru);

  for (int i = 0; i < pesanBaru; i++){
    // Chat id of the requester
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != chatId){
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }
    
    // Print the received message
    String text = bot.messages[i].text;
    Serial.println(text);

    String fromName = bot.messages[i].from_name;

    if (text == "/flash") {
      flash = !flash;
      digitalWrite(FLASHpin, flash);
    }
    if (text == "/foto") {
      sendPhoto = true;
      Serial.println("Meminta foto baru");
    }
    if (text == "/stream"){
      String ip = WiFi.localIP().toString();
      bot.sendMessage(chatId, ip, "");
    }
    if (text == "/start"){
      String welcome = "VLR - Virtual Live Alert\n";
      welcome += "klik tulisan biru\n";
      welcome += "/foto : untuk ambil gambar langsung\n";
      welcome += "/flash : untuk on off lampu flash\n";
      welcome += "/stream untuk stream\n";
      welcome += "system ini otomatis kirim foto saat terjadi gerakan.\n";
      bot.sendMessage(chatId, welcome, "Markdown");
    }
  }
}
