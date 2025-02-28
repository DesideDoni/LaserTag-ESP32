/*==========================
LASER TAG-ESP32-PLAYER 1
Code by : Doni Kurniawan
612022010
Laser tag system based on ESP-NOW and IR communication
============================*/

#include <IRremote.hpp>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <esp_now.h>
#include <WiFi.h>

// 🛠 PIN SETUP
#define TRIGGER_BUTTON 4
#define RELOAD_BUTTON 5
#define RESTART_BUTTON 18
#define IR_LED_PIN 14
#define IR_RECEIVER_PIN 15

// 🛠 GAME SETTINGS
const int MAX_AMMO = 10;
const int MAX_HP = 6;
#define PLAYER_HEX 0xA1  // Kode HEX Player 1
#define OPPONENT_HEX 0xB2  // Kode HEX Player 2

// Custom character untuk Heart
byte bullet[8] = {B00000, B00000, B01111, B11111, B11111, B01111, B00000, B00000};
byte leftHeart[8] = {B01110, B11111, B11111, B11111, B01111, B00111, B00011, B00001};
byte rightHeart[8] = {B01110, B11111, B11111, B11111, B11110, B11100, B11000, B10000};
byte leftBrokeHeart[8] = {B01110, B11111, B11100, B11111, B01100, B00111, B00010, B00001};
byte rightBrokeHeart[8] = {B01110, B00111, B11111, B00111, B11110, B00100, B11000, B00000};

// Variabel untuk menyimpan status heart (false = Heart, true = Broken Heart)
bool heartStates[12] = {false, false, false, false, false, false, false, false, false, false, false, false}; // 12 karakter (6 pasang)

// 🛠 GAME VARIABLES
int ammo = MAX_AMMO;
int hp = MAX_HP;
bool isDead = false;
bool gameOver = false;
bool isWinner = false; // Menyimpan status kemenangan pemain

// Inisialisasi LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// 🛠 ESP-NOW VARIABLES
uint8_t opponentAddress[] = {0x5C, 0x01, 0x3B, 0x69, 0x6A, 0x40}; // Ganti dengan alamat MAC Player 2

typedef struct {
    bool gameOver;    // Status game over
    bool isWinner;    // Status kemenangan
} GameData;

GameData gameData;

void setup() {
    Serial.begin(115200);

    // 🔹 Inisialisasi IR
    IrReceiver.begin(IR_RECEIVER_PIN, ENABLE_LED_FEEDBACK);
    IrSender.begin(IR_LED_PIN);

    // 🔹 Inisialisasi LCD
    lcd.init();
    lcd.backlight();
    lcd.createChar(0, bullet);
    lcd.createChar(1, leftHeart);
    lcd.createChar(2, rightHeart);
    lcd.createChar(3, leftBrokeHeart);
    lcd.createChar(4, rightBrokeHeart);

    // 🔹 Inisialisasi ESP-NOW
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, opponentAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    esp_now_register_recv_cb(OnDataRecv);

    // Tampilkan amunisi dan HP awal
    displayAmmo();
    displayHP();

    pinMode(TRIGGER_BUTTON, INPUT_PULLUP);
    pinMode(RELOAD_BUTTON, INPUT_PULLUP);
    pinMode(RESTART_BUTTON, INPUT_PULLUP);
}

void loop() {
    // 🔹 Jika game over, periksa tombol restart
    if (gameOver) {
        if (digitalRead(RESTART_BUTTON) == LOW) {
            restartGame();
        }
        return; // Hentikan eksekusi loop lebih lanjut
    }

    // 🔹 Baca sinyal IR secara non-blocking
    if (IrReceiver.decode()) {
        uint32_t receivedData = IrReceiver.decodedIRData.decodedRawData;
        Serial.print("Received IR data: ");
        Serial.println(receivedData, HEX);

        // Periksa apakah sinyal berasal dari lawan
        if (receivedData == OPPONENT_HEX && !isDead) {
            Serial.println("Opponent hit detected!");
            changeHeartState();
            displayHP(); // Perbarui tampilan HP

            bool allHeartsBroken = true;
            for (int i = 0; i <= 11; i++) {
                if (!heartStates[i]) {
                    allHeartsBroken = false;
                    break;
                }
            }
            if (allHeartsBroken) {
                isDead = true;
                gameOver = true;
                isWinner = false; // Player kalah
                displayGameOver(isWinner); // Tampilkan "YOU LOSE"
                sendGameData(true, false); // Kirim status game over
            }
        }

        // Abaikan sinyal yang dikirim sendiri
        if (receivedData == PLAYER_HEX) {
            Serial.println("Ignoring self-emitted signal");
        }

        IrReceiver.resume(); // Lanjutkan penerimaan sinyal berikutnya
    }

    // 🔹 Kode utama
    if (digitalRead(TRIGGER_BUTTON) == LOW && ammo > 0 && !isDead) {
        ammo--;
        displayAmmo(); // Perbarui tampilan amunisi
        sendIRSignal(); // Kirim sinyal IR
        delay(500); // Delay untuk menghindari multiple trigger
    }

    if (digitalRead(RELOAD_BUTTON) == LOW && !isDead) {
        ammo = MAX_AMMO;
        lcd.setCursor(0, 0);
        lcd.print("Reloaded            ");
        delay(1000);
        displayAmmo(); // Perbarui tampilan amunisi
        delay(500);
    }
}

// 🛠 FUNGSI UNTUK MENGIRIM SINYAL IR
void sendIRSignal() {
    IrReceiver.stop(); // Nonaktifkan IR receiver
    IrSender.sendNECRaw(PLAYER_HEX, 0); // Kirim sinyal IR
    delay(100); // Delay untuk menghindari self-interference
    IrReceiver.start(); // Aktifkan kembali IR receiver
}

// 🛠 LCD FUNCTIONS
void displayAmmo() {
    lcd.setCursor(0, 0); // Baris pertama
    lcd.print("Ammo: ");
    for (int i = 0; i < MAX_AMMO; i++) {
        if (i < ammo) lcd.write(byte(0));
        else lcd.print(" ");
    }
}

void displayHP() {
    lcd.setCursor(0, 1); // Baris kedua
    lcd.print("HP: ");
    for (int i = 0; i < MAX_HP; i++) {
        lcd.setCursor(4 + i * 2, 1);
        if (heartStates[i * 2]) {
            lcd.write(byte(3)); // Tampilkan heart utuh
        } else {
            lcd.write(byte(1)); // Tampilkan heart rusak
        }
        lcd.setCursor(5 + i * 2, 1);
        if (heartStates[i * 2 + 1]) {
            lcd.write(byte(4)); // Right Broken Heart
        } else {
            lcd.write(byte(2)); // Right Heart
        }
    }
}

void changeHeartState() {
    // Cari karakter pertama yang masih Heart (dari kanan ke kiri)
    for (int i = 11; i >= 0; i--) {
        if (!heartStates[i]) {
            heartStates[i] = true; // Ubah menjadi Broken Heart
            break;
        }
    }
}

void displayGameOver(bool isWinner) {
    lcd.clear();
    lcd.setCursor(0, 0);
    if (isWinner) {
        lcd.print("YOU WIN!");
    } else {
        lcd.print("YOU LOSE!");
    }
}

void restartGame() {
    // Reset semua variabel game
    ammo = MAX_AMMO;
    hp = MAX_HP;
    isDead = false;
    gameOver = false;
    isWinner = false;
    for (int i = 0; i <= 11; i++) {
        heartStates[i] = false;
    }

    // Tampilkan status awal di LCD
    displayAmmo();
    displayHP();
}

// 🛠 ESP-NOW FUNCTIONS
void sendGameData(bool isGameOver, bool isWinner) {
    gameData.gameOver = isGameOver;
    gameData.isWinner = isWinner;

    esp_err_t result = esp_now_send(opponentAddress, (uint8_t *) &gameData, sizeof(gameData));

    if (result == ESP_OK) {
        Serial.println("Data sent successfully");
    } else {
        Serial.println("Error sending data");
    }
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    memcpy(&gameData, incomingData, len);

    // Periksa apakah lawan mengirim status game over
    if (gameData.gameOver) {
        gameOver = true;
        isWinner = !gameData.isWinner; // Jika lawan kalah, pemain menang
        displayGameOver(isWinner); // Tampilkan "YOU WIN" atau "YOU LOSE"
    }
}