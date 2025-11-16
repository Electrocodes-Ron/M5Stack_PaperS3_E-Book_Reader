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
 *
 * v2: Added character replacement in textSubstring to handle all
 * common "smart" characters (curly quotes, em-dashes, ellipses, etc.).
 * Also, fixed a layout bug in displayPage to correctly handle
 * line breaks and word wrapping to match the pagination logic.
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

/**
 * @brief Extracts a substring and replaces a wide range of "smart"
 * punctuation with standard ASCII equivalents that the font can render.
 * * This function reads bytes from the textFile buffer and builds a String.
 * It checks for multi-byte UTF-8 sequences and single-byte Windows-1252
 * sequences for characters like curly quotes, em-dashes, ellipses, etc.,
 * and replaces them with standard characters (', ", -, ...).
 */
String textSubstring(uint8_t *textFile, int startPtr, int endPtr) {
  String s = "";
  // Pre-allocate memory, add a little extra for replacements like ... and (c)
  s.reserve(endPtr - startPtr + 50); 

  for (int i = startPtr; i < endPtr; i++) {
    uint8_t b1 = textFile[i];

    // --- Check for 3-byte UTF-8 sequences (E2 80 xx) ---
    if (b1 == 0xE2 && i + 2 < endPtr) {
      uint8_t b2 = textFile[i+1];
      uint8_t b3 = textFile[i+2];

      if (b2 == 0x80) {
        switch (b3) {
          case 0x98: // ‘ (Left single quote)
          case 0x99: // ’ (Right single quote / apostrophe)
          case 0xB2: // ′ (Prime)
            s += '\'';
            i += 2;
            continue;
          case 0x9C: // “ (Left double quote)
          case 0x9D: // ” (Right double quote)
            s += '"';
            i += 2;
            continue;
          case 0x94: // — (Em dash)
            s += '-';
            i += 2;
            continue;
          case 0xA6: // … (Ellipsis)
            s += "...";
            i += 2;
            continue;
          case 0xA2: // • (Bullet)
            s += '*';
            i += 2;
            continue;
        }
      }
    }

    // --- Check for 2-byte UTF-8 sequences (C2 xx) ---
    if (b1 == 0xC2 && i + 1 < endPtr) {
      uint8_t b2 = textFile[i+1];
      
      if (b2 == 0xA9) { // © (Copyright)
        s += "(c)";
        i += 1;
        continue;
      }
    }

    // --- Check for 1-byte Windows-1252 sequences ---
    switch (b1) {
      case 0x91: // ‘ (Left single quote)
      case 0x92: // ’ (Right single quote / apostrophe)
        s += '\'';
        continue;
      case 0x93: // “ (Left double quote)
      case 0x94: // ” (Right double quote)
        s += '"';
        continue;
      case 0x97: // — (Em dash)
        s += '-';
        continue;
      case 0x85: // … (Ellipsis)
        s += "...";
        continue;
      case 0x95: // • (Bullet)
        s += '*';
        continue;
      case 0xA9: // © (Copyright)
        s += "(c)";
        continue;
    }

    // --- If no special character matched, add the byte as a char ---
    // Pass through all other characters, including \r and \n
    s += (char) b1;
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
    
    // Handle end of file case
    if (wordEnd == -1 - startPtr) {
        wordEnd = textLength - startPtr;
    }

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

// Helper function to find next space or CR in a String
int findNextWordBreak(String &s, int start) {
  for (int i = start; i < s.length(); i++) {
    if (s.charAt(i) == ' ' || s.charAt(i) == '\r') {
      return i;
    }
  }
  return -1; // No break found
}

/**
 * @brief Displays a page of text, correctly handling word wrap and newlines.
 * * This function re-creates the layout logic from getNextPage, but
 * actually prints the words to the display. It processes the page text
 * word by word, where words are separated by ' ' or '\r'.
 */
void displayPage(uint8_t *textFile, aPage page) {
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(border, border + 20);
  
  // Get the processed text for the entire page
  String text = textSubstring(textFile, page.start, page.end);
  
  int wordStart = 0;
  int wordEnd = 0;

  // Loop through the text, word by word
  while (wordStart < text.length()) {
    
    // Check for and handle leading spaces or CRs
    if (text.charAt(wordStart) == ' ' || text.charAt(wordStart) == '\r') {
      if (text.charAt(wordStart) == '\r') {
        // Handle explicit newline
        M5.Display.setCursor(border, M5.Display.getCursorY() + 22);
      } else {
        // Handle space - print it to advance cursor
        M5.Display.print(' ');
      }
      wordStart++; // Move to next character
      continue; // Loop again
    }

    // Now wordStart is at the beginning of a word
    wordEnd = findNextWordBreak(text, wordStart);
    
    // If no more breaks, the rest of the string is the last word
    if (wordEnd == -1) {
      wordEnd = text.length();
    }

    // Extract the word
    String word = text.substring(wordStart, wordEnd);
    uint16_t len = M5.Display.textWidth(word.c_str());
    
    // Check if the word fits on the current line
    // (Only check if we're not at the beginning of the line)
    if (M5.Display.getCursorX() > border && (M5.Display.getCursorX() + len >= SCREEN_WIDTH - border)) {
      // It doesn't fit, move to the next line
      M5.Display.setCursor(border, M5.Display.getCursorY() + 22);
    }
    
    // Print the word
    M5.Display.print(word);
    
    // Move to the start of the next token (which is at wordEnd)
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
  M5.Display.setFont(&fonts::FreeSans9pt7b);
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