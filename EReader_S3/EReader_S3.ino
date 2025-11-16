/*
 * E-Book reader for M5Paper S3
 * 
 * Reads a book from the SD card: "/book.txt"
 * Touch the screen: Left side (previous), Middle (sleep), Right side (next)
 * Page number is saved to SD card
 * 
 * Required libraries:
 * - M5Unified (>= 0.2.5)
 * - M5GFX (>= 0.2.7)
 */

#include <M5Unified.h>
#include <SD.h>

const char *DATA_FILE = "/pageNumber.txt";

int currentPage = 0;
int pageCount = 0;
int border = 10;
uint8_t *textFile;

struct aPage {
  uint32_t start;
  uint32_t end;
} pages[2000];

// Touch areas for navigation (divided into thirds)
const int SCREEN_WIDTH = 540;
const int SCREEN_HEIGHT = 960;
const int TOUCH_LEFT = SCREEN_WIDTH / 3;
const int TOUCH_RIGHT = (SCREEN_WIDTH * 2) / 3;

#define SD_SPI_CS_PIN   47
#define SD_SPI_SCK_PIN  39
#define SD_SPI_MOSI_PIN 38
#define SD_SPI_MISO_PIN 40

String textSubstring(uint8_t *textFile, int startPtr, int endPtr) {
  String s = "";
  for (int i = startPtr; i < endPtr; i++) {
    s += (char) textFile[i];
  }
  return s;
}

int textIndexOfSpaceCR(uint8_t *textFile, int startPtr, int textLength) {
  for (int ptr = startPtr; ptr < textLength; ptr++) {
    if ( (textFile[ptr] == 32) || (textFile[ptr] == 13) ) {
      return ptr;
    }
  }
  return -1;
}

bool reachedEndOfBook(int wordStart, int textLength) {
  return wordStart >= textLength;
}

void storePageSD(uint32_t c) {
  File f = SD.open(DATA_FILE, FILE_WRITE);
  if (f) {
    uint8_t buf[4];
    buf[0] = c;
    buf[1] = c >> 8;
    buf[2] = c >> 16;
    buf[3] = c >> 24;
    f.write(buf, 4);
    f.close();
  }
}

uint32_t getPageSD() {
  uint32_t val = 0;
  if (SD.exists(DATA_FILE)) {
    File f = SD.open(DATA_FILE, FILE_READ);
    if (f && f.available() >= 4) {
      uint8_t buf[4];
      f.read(buf, 4);
      val = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
      f.close();
    }
  }
  return val;
}

int getNextPage(uint8_t *textFile, int startPtr, int textLength) {
  int wordStart = 0;
  int wordEnd = 0;
  int xPos = 0;
  int yPos = 0;
  int xMax = SCREEN_WIDTH - (border << 1);
  int yMax = SCREEN_HEIGHT - (border << 1) - 60; // Leave space for footer
  
  M5.Display.setTextSize(1);
  
  while ( !reachedEndOfBook(wordStart + startPtr, textLength - 500) ) {
    wordEnd = textIndexOfSpaceCR(textFile, startPtr + wordStart + 1, textLength) - startPtr;
    String text = textSubstring(textFile, startPtr + wordStart, startPtr + wordEnd);
    int textPixelLength = M5.Display.textWidth(text.c_str());
    
    if ((xPos + textPixelLength >= xMax) || (text.charAt(0) == 13) ) {
      xPos = 0;
      yPos += 22;
      wordStart++;
      
      if ( ( yPos + 90 ) >= yMax ) {
        return startPtr + wordStart;
      }
    }
    xPos += textPixelLength;
    wordStart = wordEnd;
  }
  return textLength;
}

void findPageStartStop(uint8_t *textFile, int textLength) {
  int startPtr = 0;
  int endPtr = 0;
  
  while ( (endPtr < textLength) && (textFile[endPtr] != 0) ) {
    endPtr = getNextPage(textFile, startPtr, textLength);
    if (startPtr >= textLength) break;
    
    pages[pageCount].start = startPtr;
    pages[pageCount].end = endPtr;
    pageCount++;
    startPtr = endPtr;
  }
}

void displayPage(uint8_t *textFile, aPage page) {
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(border, border + 20);
  
  String text = textSubstring(textFile, page.start, page.end);
  int wordStart = 0;
  int wordEnd = 0;
  
  while ( (text.indexOf(' ', wordStart) >= 0) && ( wordStart <= text.length())) {
    wordEnd = text.indexOf(' ', wordStart + 1);
    if (wordEnd == -1) wordEnd = text.length();
    
    String word = text.substring(wordStart, wordEnd);
    uint16_t len = M5.Display.textWidth(word.c_str());
    
    if (M5.Display.getCursorX() + len >= SCREEN_WIDTH - border) {
      M5.Display.setCursor(border, M5.Display.getCursorY() + 22);
      wordStart++;
    }
    M5.Display.print(word);
    wordStart = wordEnd;
  }

  // Draw footer with page number
  M5.Display.setCursor(border, SCREEN_HEIGHT - 40);
  char footer[256];
  sprintf(footer, "Page %d of %d", currentPage + 1, pageCount);
  M5.Display.print(footer);
  
  // Draw instructions
  M5.Display.setCursor(SCREEN_WIDTH - 200, SCREEN_HEIGHT - 40);
  M5.Display.print("Tap: <   Sleep   >");
}

void setup() {
  Serial.begin(115200);
  
  // Initialize M5Unified
  // auto cfg = M5.config();
  M5.begin();
  
  // Initialize display
  M5.Display.setRotation(0);
  M5.Display.setFont(&fonts::FreeSansOblique12pt7b);
  M5.Display.setTextSize(1);
  
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setCursor(10, 50);
  M5.Display.println("Initializing SD...");
  
  // SD Card Initialization
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    // Print a message if SD card initialization failed or if the SD card does not exist.
    M5.Display.print("\n SD card not detected\n");
    while (1)
      ;
  } else {
    M5.Display.print("\n SD card detected\n");
  }
  M5.Display.println("SD Card OK");
  
  // Allocate memory for text file
  textFile = (uint8_t*)ps_malloc(1048576); // 1MB
  if (!textFile) {
    Serial.println("Memory allocation failed!");
    M5.Display.println("Memory Error!");
    while(1) delay(1000);
  }
  
  M5.Display.println("Memory allocated");
  
  // Load book
  File f = SD.open("/book.txt", FILE_READ);
  if (!f) {
    Serial.println("Could not open book.txt!");
    M5.Display.println("Book not found!");
    M5.Display.println("Place 'book.txt' on SD card");
    while(1) delay(1000);
  }
  
  int fileLength = f.available();
  Serial.print("File length: ");
  Serial.println(fileLength);
  M5.Display.print("Loading book: ");
  M5.Display.print(fileLength);
  M5.Display.println(" bytes");
  
  f.read(textFile, fileLength);
  f.close();
  Serial.println("Loaded.");

  // Split into pages
  M5.Display.println("Splitting into pages...");
  Serial.println("Splitting into pages.");
  findPageStartStop(textFile, fileLength);
  Serial.print("Total pages: ");
  Serial.println(pageCount);
  
  M5.Display.print("Total pages: ");
  M5.Display.println(pageCount);
  delay(1000);

  // Load saved page
  currentPage = getPageSD();
  if (currentPage >= pageCount) currentPage = 0;
  
  displayPage(textFile, pages[currentPage]);
}

void loop() {
  M5.update();
  
  // Check for touch input
  auto t = M5.Touch.getDetail();
  
  if (t.wasPressed()) {
    int x = t.x;
    int y = t.y;
    
    Serial.print("Touch at: ");
    Serial.print(x);
    Serial.print(", ");
    Serial.println(y);
    
    // Left third of screen - previous page
    if (x < TOUCH_LEFT) {
      if (--currentPage < 0) currentPage = 0;
      displayPage(textFile, pages[currentPage]);
      delay(200); // Debounce
    }
    // Right third of screen - next page
    else if (x > TOUCH_RIGHT) {
      if (++currentPage >= pageCount) currentPage = pageCount - 1;
      displayPage(textFile, pages[currentPage]);
      delay(200); // Debounce
    }
    // Middle third - save and sleep
    else {
      storePageSD(currentPage);
      M5.Display.fillScreen(TFT_WHITE);
      M5.Display.setCursor(SCREEN_WIDTH/2 - 50, SCREEN_HEIGHT/2);
      M5.Display.print("Sleeping...");
      delay(1000);
      M5.Power.powerOff();
    }
  }
  
  delay(50);
}