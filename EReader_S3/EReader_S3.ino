/*
 * E-Book reader for M5Paper S3
 * * v5: 
 * - SYNCHRONIZED LAYOUT ENGINE: getNextPage now mirrors displayPage logic 1:1.
 * This fixes the "overflowing text" and "empty bottom space" issues.
 * - Both functions now process text as "Tokens" (Space, Newline, or Word) 
 * to ensure the calculated page ending exactly matches the displayed page.
 */

#include <M5Unified.h>
#include <SD.h>
#include <ctype.h>

const char *DATA_FILE = "/pageNumber.txt";

int currentPage = 0;
int pageCount = 0;
int border = 10;
int lineHeight = 22; // Standardized line height
uint8_t *textFile;

struct aPage {
  uint32_t start;
  uint32_t end;
} pages[2000];

const int SCREEN_WIDTH = 540;
const int SCREEN_HEIGHT = 960;
const int TOUCH_LEFT = SCREEN_WIDTH / 3;
const int TOUCH_RIGHT = (SCREEN_WIDTH * 2) / 3;

#define SD_SPI_CS_PIN   47
#define SD_SPI_SCK_PIN  39
#define SD_SPI_MOSI_PIN 38
#define SD_SPI_MISO_PIN 40

// --- String Helper ---
String textSubstring(uint8_t *textFile, int startPtr, int endPtr) {
  String s = "";
  s.reserve(endPtr - startPtr + 20); 

  for (int i = startPtr; i < endPtr; i++) {
    uint8_t b1 = textFile[i];
    
    // UTF-8 3-byte
    if (b1 == 0xE2 && i + 2 < endPtr) {
      uint8_t b3 = textFile[i+2];
      if (textFile[i+1] == 0x80) {
        switch (b3) {
          case 0x98: case 0x99: case 0xB2: s += '\''; i += 2; continue;
          case 0x9C: case 0x9D: s += '"'; i += 2; continue;
          case 0x94: s += '-'; i += 2; continue;
          case 0xA6: s += "..."; i += 2; continue;
          case 0xA2: s += '*'; i += 2; continue;
        }
      }
    }
    // UTF-8 2-byte
    if (b1 == 0xC2 && i + 1 < endPtr) {
      if (textFile[i+1] == 0xA9) { s += "(c)"; i += 1; continue; }
    }
    // Windows-1252
    switch (b1) {
      case 0x91: case 0x92: s += '\''; continue;
      case 0x93: case 0x94: s += '"'; continue;
      case 0x97: s += '-'; continue;
      case 0x85: s += "..."; continue;
      case 0x95: s += '*'; continue;
      case 0xA9: s += "(c)"; continue;
    }
    s += (char) b1;
  }
  return s;
}

void storePageSD(uint32_t c) {
  File f = SD.open(DATA_FILE, FILE_WRITE);
  if (f) {
    uint8_t buf[4];
    buf[0] = c; buf[1] = c >> 8; buf[2] = c >> 16; buf[3] = c >> 24;
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

// --- CORE LOGIC: Find end of word ---
int findNextWordBreak(uint8_t *textFile, int start, int maxLen) {
  for (int i = start; i < maxLen; i++) {
    uint8_t c = textFile[i];
    if (c == ' ' || c == '\r' || c == '\n') return i;
  }
  return maxLen;
}

// --- CORE LOGIC: Page Calculator ---
// This now accurately simulates the drawing process to find EXACT page breaks
int getNextPage(uint8_t *textFile, int startPtr, int textLength) {
  
  M5.Display.setTextSize(1);
  
  int xCursor = border;
  int yCursor = border + 20;
  
  int xMax = SCREEN_WIDTH - border;
  int yMax = SCREEN_HEIGHT - 40; // Stop above footer
  
  int ptr = startPtr;
  
  while (ptr < textLength) {
    uint8_t c = textFile[ptr];
    
    // 1. Handle Newlines
    if (c == '\r') {
      xCursor = border;
      yCursor += lineHeight;
      ptr++; // Consume CR
      
      // Check if we walked off the bottom
      if (yCursor > yMax) return ptr - 1; // End page BEFORE this newline
      continue;
    }
    
    // 2. Handle Line Feeds (Ignore them, usually follow CR)
    if (c == '\n') {
      ptr++; 
      continue;
    }
    
    // 3. Handle Spaces
    if (c == ' ') {
      int w = M5.Display.textWidth(" ");
      
      // If space fits, print it (advance cursor)
      // Note: Spaces at end of line usually don't trigger wrap in modern engines, 
      // but for simple drawing, they just advance X.
      if (xCursor + w > xMax) {
         xCursor = border;
         yCursor += lineHeight;
         // Space usually eaten by wrap, but we just reset X
      } else {
         xCursor += w;
      }
      
      ptr++; // Consume Space
      
      if (yCursor > yMax) return ptr - 1;
      continue;
    }
    
    // 4. Handle Words
    int wordEnd = findNextWordBreak(textFile, ptr, textLength);
    String word = textSubstring(textFile, ptr, wordEnd);
    int w = M5.Display.textWidth(word.c_str());
    
    // Wrap logic: If not at start of line AND word doesn't fit...
    if (xCursor > border && (xCursor + w >= xMax)) {
       xCursor = border;
       yCursor += lineHeight;
    }
    
    // Check Vertical Bounds BEFORE placing word
    // Note: We use a slightly conservative check (yCursor + lineHeight * 0.5) 
    // to ensure we don't print partial lines at the bottom.
    if (yCursor > yMax - (lineHeight/2)) {
       return ptr; // Return start of this word (it goes to next page)
    }
    
    // "Print" the word
    xCursor += w;
    ptr = wordEnd;
  }
  
  return textLength; // Reached End of File
}

void findPageStartStop(uint8_t *textFile, int textLength) {
  int startPtr = 0;
  int endPtr = 0;
  pageCount = 0;
  
  while (startPtr < textLength) {
    endPtr = getNextPage(textFile, startPtr, textLength);
    
    // Safety: ensure progress
    if (endPtr <= startPtr) {
        // Force advance if stuck (e.g. giant word)
        endPtr = startPtr + 1;
        // Or scan to next space
        int nextSpace = findNextWordBreak(textFile, startPtr + 1, textLength);
        if(nextSpace > startPtr) endPtr = nextSpace;
    }
    
    pages[pageCount].start = startPtr;
    pages[pageCount].end = endPtr;
    pageCount++;
    
    if (pageCount >= 2000) break; // Safety limit
    
    startPtr = endPtr;
  }
}

void displayPage(uint8_t *textFile, aPage page) {
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(border, border + 20);
  
  int ptr = page.start;
  int endPtr = page.end;
  int xMax = SCREEN_WIDTH - border;
  
  while (ptr < endPtr) {
    uint8_t c = textFile[ptr];
    
    // Newline
    if (c == '\r') {
      M5.Display.setCursor(border, M5.Display.getCursorY() + lineHeight);
      ptr++;
      continue;
    }
    // Linefeed
    if (c == '\n') {
      ptr++;
      continue;
    }
    // Space
    if (c == ' ') {
      // Explicitly handle wrap for space to match calculator
      if (M5.Display.getCursorX() + M5.Display.textWidth(" ") > xMax) {
         M5.Display.setCursor(border, M5.Display.getCursorY() + lineHeight);
      } else {
         M5.Display.print(' ');
      }
      ptr++;
      continue;
    }
    
    // Word
    int wordEnd = findNextWordBreak(textFile, ptr, endPtr);
    // If word extends beyond page boundary (shouldn't happen if logic is perfect), clamp it
    if (wordEnd > endPtr) wordEnd = endPtr; 
    
    String word = textSubstring(textFile, ptr, wordEnd);
    int w = M5.Display.textWidth(word.c_str());
    
    if (M5.Display.getCursorX() > border && (M5.Display.getCursorX() + w >= xMax)) {
       M5.Display.setCursor(border, M5.Display.getCursorY() + lineHeight);
    }
    
    M5.Display.print(word);
    ptr = wordEnd;
  }

  // Footer
  M5.Display.setCursor(border, SCREEN_HEIGHT - 40);
  char footer[256];
  sprintf(footer, "Page %d of %d", currentPage + 1, pageCount);
  M5.Display.print(footer);
  
  M5.Display.setCursor(SCREEN_WIDTH - 200, SCREEN_HEIGHT - 40);
  M5.Display.print("Tap: <   Sleep   >");
}

void setup() {
  Serial.begin(115200);
  M5.begin();
  M5.Display.setRotation(0);
  M5.Display.setFont(&fonts::FreeSansOblique12pt7b);
  M5.Display.setTextSize(1);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setCursor(10, 50);
  
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    M5.Display.print("\n SD card not detected\n");
    while (1);
  }
  
  textFile = (uint8_t*)ps_malloc(1048576); 
  if (!textFile) {
    M5.Display.println("Memory Error!");
    while(1) delay(1000);
  }
  
  File f = SD.open("/book.txt", FILE_READ);
  if (!f) {
    M5.Display.println("Book not found!");
    while(1) delay(1000);
  }
  
  int fileLength = f.available();
  f.read(textFile, fileLength);
  f.close();
  
  // Trim trailing whitespace
  while (fileLength > 0 && isspace(textFile[fileLength - 1])) {
    fileLength--;
  }
  
  M5.Display.println("Pagination...");
  findPageStartStop(textFile, fileLength);
  
  currentPage = getPageSD();
  if (currentPage >= pageCount) currentPage = 0;
  
  displayPage(textFile, pages[currentPage]);
}

void loop() {
  M5.update();
  auto t = M5.Touch.getDetail();
  
  if (t.wasPressed()) {
    int x = t.x;
    if (x < TOUCH_LEFT) {
      if (--currentPage < 0) currentPage = 0;
      displayPage(textFile, pages[currentPage]);
    }
    else if (x > TOUCH_RIGHT) {
      if (++currentPage >= pageCount) currentPage = pageCount - 1;
      displayPage(textFile, pages[currentPage]);
    }
    else {
      storePageSD(currentPage);
      M5.Display.fillScreen(TFT_WHITE);
      M5.Display.setCursor(SCREEN_WIDTH/2 - 50, SCREEN_HEIGHT/2);
      M5.Display.print("Sleeping...");
      delay(1000);
      M5.Power.powerOff();
    }
    delay(200);
  }
  delay(50);
}