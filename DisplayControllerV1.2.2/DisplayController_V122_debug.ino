
/*
 * ^*******************************************************************
 * 7-Serment Display Controller V0.1, 19.01.2022 -jb
 * based on Arduino Nano.
 * 
 * Designed for common anode displays.
 * Max. 8 7-segment elements addressed. 
 * Digit numbering: 8 7 6 5 4 3 2 1
 * Segment numbering:     a
 *                      f   b
 *                        g
 *                      e   c
 *                        d
 * Implemented serial protocols (RS232)
 *
 *  - F5B:  Mode7, 9600bd, 8N1, start-sequence: ESC, '0', 'A', '0', then 7 ascii chars, digits only (no MODE jumper)
 *          LF and CR are ignored, space blanks a digit
 *  - F5B:  Mode7, 9600bd, 8N1, start-char: ESC, 7 ascii chars, digits only (no longer implemented)
 *          LF and CR are ignored, space blanks a digit
 *  - F5J:  Mode6,  9600bd, 8N1, start-char: 'A'/'R', 4/12 ascii chars, (MODE jumper 1 set)
 *          Mode5, 19200bd, 8N1, start-char: 'A'/'R', 4/12 ascii chars, (MODE jumper 2 set)
 *          Mode4,  9600bd, 8N1, start-char: 'A', 2x412 ascii chars,    (MODE jumper 1 & 2 set)
 *                   
 * 
 **********************************************************************
 */

// DC8D, V1.2.2 DEBUG
//
// V1.2.2 bug fix in  shiftOutDigit() for dots when leading zero is blanked out
//        bug fix flash pulse generation
// V1.2.0 flashing dots for F3K via GliderScore for F5J extended protocol (function updateSerialProtocolF5J())
// V1.1.3 Allow '-' to be shifted out for F5J extended protocol (function shiftOutDigit())
// V1.1.2 Implement F5J extended protocol with automatic switch-over to standard protocol
//        new function updateSerialProtocolF5J()
//
#define PIN_SHIFT   5   // connected to SRCK
#define PIN_STORE   3   // connected to RCK
#define PIN_DATA    7   // connected to SERIN
#define PIN_nSRCLR  6   // connected to nSRCLR
#define PIN_G1      4   // connected to nG1 (output display 1-4 disable)
#define PIN_G2      8   // connected to nG2 (output display 5-8 disable)
#define PIN_MODE4   A2
#define PIN_MODE3   A3
#define PIN_MODE2   A4
#define PIN_MODE1   A5

#define MaxDigits     8
#define MaxSegments   8
#define NoOfDigitsF5B 7 // F5B sign
#define NoOfDigitsF5J 4 // F5J sign
#define f5jBufLen     16
#define f5jStandardProtoLen  6
#define f5jExtendedProtoLen  14

#define ESC 27
// escape code
#define CR  13
#define LF  10

#define WD_TIME 3000    // timeout in ms
#define LS_TIME1 1000   // slow blinking when communication timed out
#define LS_TIME2 200    // fast flashing when communication active

const byte digits[10] = {
  0b00111111, // 0
  0b00000110, // 1
  0b01011011, // 2
  0b01001111, // 3
  0b01100110, // 4
  0b01101101, // 5
  0b01111101, // 6
  0b00000111, // 7 (min. segments)
  0b01111111, // 8
  0b01101111  // 9
};

unsigned char  incomingByte = 0;   // for incoming serial data
char  buf[MaxDigits+1];   // ASCII buffer for number string
char  f5jBuf[16];         // ASCII buffer protocol string
char  rBuf[2];            // F5J Round buffer
char  gBuf[2];            // F5J Group buffer
char  tBuf[4];            // F5J Time buffer

unsigned char  f5jProtoMode = 0;
unsigned char  f5jTimerState = ' ';
unsigned char  displayRoundGroup = false;
unsigned char  noOfDigits = 0;
unsigned char  mode = 0;
unsigned char  mode_old = 0;
unsigned char  charCnt = 0;
unsigned char  timeout = false;
unsigned long  lsTime = LS_TIME1;
unsigned long  timecnt = 0;
unsigned long  watchdogCnt = 0;

unsigned char timeType = 'U';
unsigned char flashPulse = false;

unsigned long currentTime = 0;
unsigned long previousTime = 0;

unsigned char getMode(void) {
unsigned char md;
  md = 0;
  md |= digitalRead(PIN_MODE1);
  md |= digitalRead(PIN_MODE2) << 1;
  md |= digitalRead(PIN_MODE3) << 2;
  return md;
}

void shiftOutDigit(char digit) {
  unsigned char no, dot;
  dot = false;
  if (isDigit((digit & 0x7F))) {  // allow for dot indicator in MSB
    if (digit & 0x80)
      dot = true;
    no = digits[(digit & 0x7F) -'0']; // dot indication removed for table access
  }
  else
    if ((digit == '-') && (f5jProtoMode == 1))
      no = 0b01000000;    // allow '-' for Round/Group-Display in extended F5J protocol
    else {
      if (digit & 0x80)
        dot = true;
      no = 0b00000000;    // blank digit if blank char or error
    }
  if (dot)
    no |= 0x80;           // add dot bit if necessary
  shiftOut(PIN_DATA, PIN_SHIFT, MSBFIRST, no);
// debug
  Serial.print(" ");
  if (no & 0x80) {
    Serial.print((no & 0x7F));
    Serial.print("Dot");
  }
  else {
    Serial.print(no);
  }
//

}

//                                      P1  P2  P3   P4   P5   P6  P7   P8
// expected shift buffer content 0..7: 10m, 1m, 10s, 1s, (10m, 1m, 10s, 1s)
//
// F3K: lower dot: decimal dot of P2, upper dot: decimal dot of P1
//
char ch;
void shiftOutNumberStringF5J(char * AsciiNoString){
  // blank out leading zeros
  if (AsciiNoString[0] == '0') AsciiNoString[0] = ' '; // 10m first P group
  if (AsciiNoString[4] == '0') AsciiNoString[4] = ' '; // 10m second P group
  digitalWrite(PIN_STORE, LOW);
  // shift aut data for last panel (8) first
  for(byte i=0; i<MaxDigits; i++) {
    ch = *(AsciiNoString+MaxDigits-i-1);
    // handle dot control
    if (timeType == 'W') {
      if ((i == MaxDigits-2) || (i == MaxDigits-1)) ch |= 0x80; // both dots on
    }
    if (timeType == 'L') {
      if ((i == MaxDigits-2) || (i == MaxDigits-1)) //both dots flashing
        if (flashPulse) ch |= 0x80;
    }
    if (timeType == 'N') {              //both dots alternately flashing
      if (flashPulse) {
        if (i == MaxDigits-2) ch |= 0x80;
      } else {
        if (i == MaxDigits-1) ch |= 0x80;
      }
    }
    if (timeType == 'P') {
      if (i == MaxDigits-2) ch |= 0x80;  // lower dot on 
    }
    if (timeType == 'T') {
      if (i == MaxDigits-1) ch |= 0x80;  // upper dot on  
    }
    if (timeType == 'D') {
      if ((i == MaxDigits-2) || (i == MaxDigits-1)) ch |= 0x80; // both dots on
    }
    if (timeType == 'S') {
      if ((i == MaxDigits-2) || (i == MaxDigits-1)) ch |= 0x80; // both dots on
    }
    shiftOutDigit(ch);
// debug
  Serial.print(" ");  Serial.print((ch & 0x7F));
//
  }
// debug
  Serial.println();
//

  digitalWrite(PIN_STORE, HIGH);
}

//                                      P1    P2    P3    P4     P5    P6    P7    P8
// expected shift buffer content 0..7: 10SN, 1SN, 10STRE, 1STRE, 100s, 10s, 1s, blank
//
void shiftOutNumberStringF5BLegacy(char * AsciiNoString){
  // blank out leading zeros
  if (AsciiNoString[0] == '0') AsciiNoString[0] = ' '; // 10SN
  if (AsciiNoString[2] == '0') AsciiNoString[2] = ' '; // 10STRE
  if (AsciiNoString[4] == '0') AsciiNoString[4] = ' '; // 100s
  if ((AsciiNoString[5] == '0')&&(AsciiNoString[4] == '0')) AsciiNoString[5] = ' '; // 10s
  digitalWrite(PIN_STORE, LOW);
  // shift aut data for last panel (8) first
  for(byte i=0; i<MaxDigits; i++) 
    shiftOutDigit(*(AsciiNoString+MaxDigits-i-1));
  digitalWrite(PIN_STORE, HIGH);
}

//                                      P1    P2    P3    P4     P5    P6    P7    P8
// expected shift buffer content 0..7: blank, 100s, 10s, 1s, 10SN, 1SN, 10STRE, 1STRE
//
void shiftOutNumberStringF5BMarco(char * AsciiNoString){
  char cBuf[MaxDigits];
  // blank out leading zeros
  if (AsciiNoString[0] == '0') AsciiNoString[0] = ' '; // 10SN
  if (AsciiNoString[2] == '0') AsciiNoString[2] = ' '; // 10STRE
  if (AsciiNoString[4] == '0') AsciiNoString[4] = ' '; // 100s
  if ((AsciiNoString[5] == '0')&&(AsciiNoString[4] == '0')) AsciiNoString[5] = ' '; // 10s
  // re-arrange shift buffer..
  cBuf[0] =  AsciiNoString[7];
  cBuf[1] =  AsciiNoString[4];
  cBuf[2] =  AsciiNoString[5];
  cBuf[3] =  AsciiNoString[6];
  cBuf[4] =  AsciiNoString[0];
  cBuf[5] =  AsciiNoString[1];
  cBuf[6] =  AsciiNoString[2];
  cBuf[7] =  AsciiNoString[3];
  digitalWrite(PIN_STORE, LOW);
  // shift aut data for last panel (8) first
  for(byte i=0; i<MaxDigits; i++) 
    shiftOutDigit(*(cBuf+MaxDigits-i-1));
  digitalWrite(PIN_STORE, HIGH);
}

//
// F5B protocol.
// Returns true if valid telegram (isDigit) with noOfDigits received.
//
uint32_t keyWord = 0;
uint8_t  rxState = 0;

//
// string sent: { 27,48,65,48,stnoh,stnol,strh,strl,ti2,ti1,ti0,13};
//
byte updateSerialProtocolF5B(void) {
  while (Serial.available()) {
    incomingByte = Serial.read();
    switch (rxState) {
      
      case 0:
        // find start key (ESC, '0', 'A', '0') in serial stream
        keyWord = (keyWord << 8) | (uint8_t) incomingByte;
        if (keyWord == 0x1B304130) {
          // keyword found, clear receive buffer
          for (int i = 0; i < MaxDigits; i++) {
            buf[i]=' ';
          }
          keyWord = 0;
          charCnt = 0;
          rxState = 1;
        }
        break;
        
      case 1:
        // collect message body (7 ASCII chars)
        if (charCnt < noOfDigits) {
          if ((isDigit(incomingByte)) || (incomingByte == ' ')) { // allow character blanking with a space
            buf[charCnt++] = incomingByte;
          }
          else {
            // invalid char in message payload. Skip entire message.
            rxState = 0;
            break;
          }
        }
        if (charCnt == noOfDigits)
          rxState = 2;
        break;
        
      case 2:
        // find end key (CR)    
        if (incomingByte == CR) {
          rxState = 0;
          return true;
        } else {
          if (incomingByte != LF) {
            // allow LF for easy debugging
            // invalid char in telegram. Skip entire message.
            rxState = 0;
          }
        }
        break;

    } // switch
  } // while
  return false;
}

//Test pattern:
//char inbuf[] = " R23G01T0246PT\r R23G01T0245PT\r R23G01T0146PT\r R23G01T0145PT\r      ";
//char inbuf[] = " R23G01T1446WT\r R23G01T0445WT\r R23G01T0346WT\r R23G01T0045WT\r      ";
//char inbuf[] = " R23G01T0000WT\r R23G01T0001WT\r R23G01T0315WT\r R23G01T0216WT\r      ";
//char inbuf[] = "ER23G01T0400WT\rER23G01T0501WT\rER23G01T0915WT\rER23G01T0216WT\rA1234\r";
char inbuf[] = " R23G01T0946PT\r R23G01T0945TT\r R23G01T0944WT\r R23G01T0857NF\r R23G01T0856NF\r R23G01T0855LT\r            ";
unsigned char inbufPtr = 0;

byte inbufAvail(void) {
//  if (inbufPtr < 66)
  if (inbufPtr < 96)
    return true;
  else
    return false;
}

char inbufRead(void) {
  if (inbufAvail())
    return inbuf[inbufPtr++];
  else
    return 'E';
}



//
// F5J protocol
// Returns true if valid message received.
// Covers Standard Glider Score format “Ammss+CR"
// as well as the Extended Glide Score format "R99G99TmmssAA+CR"
// with AA: 'PT', 'WT', 'LT', 'ST', 'DT'.
//
// For F3K also managed: 'TT', 'NF' (Testing Time, No Flying);
// - Pandora-Telegramm (mit 'P' beginnend) wird ignoriert (100ms nach F5J Telegramm)
// - Die Runden und Gruppenangaben erscheinen nur noch in der PT, bei allen anderen Zeiten ist sie entfernt.
// - TT Time auch anzeigen (Ergänzung DUH, NF auch anzeigen).
//    Anzeige des Doppelpunktes [ MM : SS ] wie folgt:
//    -PT = unterer Punkt leuchtet
//    -TT = oberer Punkt leuchtet
//    -NF = beide Punkte blinken abwechselnd (neue Angabe DUH)
//    -WT = beide Punkte leuchten
//    -LT = beide Punkte blinken gleichzeitig
//
byte updateSerialProtocolF5J(void) {
  
//  while (Serial.available()) {   
//    incomingByte = Serial.read();

// debug
  while (inbufAvail()) {
    
    incomingByte = inbufRead();
//    Serial.print(incomingByte);Serial.print(" ");Serial.print(inbufPtr);
//    
    if (incomingByte == 'A') {
        // start of new message of standard type
        charCnt = 0;
        for (int i=0; i<MaxDigits; i++) {
          buf[i]=' ';
        }
        f5jProtoMode = 0;  // standard protocol
        return false;
    } else {
      if (incomingByte == 'R') {
        // start of new message of extended type
        for (int i=0; i<MaxDigits; i++) {
          buf[i]=' ';
        }
        for (int i=0; i<f5jBufLen; i++) {
          f5jBuf[i]=' ';
        }
        f5jBuf[0] = incomingByte;
        charCnt = 1;
        f5jProtoMode = 1;  // extended protocol
        return false;
      } else {
        if ((incomingByte == 'P') && (charCnt != 11)) { // unfortunately start char 'P' is also part of normal F5J payload
          //Start of Pandora telegram
          f5jProtoMode = 2;  // Pandora-Protocol; will be ignored
          return false;
        }
      }
    }
    
    if (f5jProtoMode == 0) {
       //fill buffer in standard mode
       if (charCnt < noOfDigits) {
          if (isDigit(incomingByte)) {
            buf[charCnt] = incomingByte;
            charCnt++;
          }
          else {
            // invalid char in message. Skip message.
            charCnt = 0xff;
          }
          return false;
        }
        else {
          if ((incomingByte == CR) && (charCnt == noOfDigits)) {
            charCnt++;   // invalidate all other incoming char
            // message complete.
//    Serial.println("Standard");
//    buf[4] = 0;
//    Serial.println(buf);

            timeType = 'U';
            return true;
          }
        }
       
    }
      
    if (f5jProtoMode == 1) {
      //fill buffer in extended mode
      if (charCnt < f5jExtendedProtoLen-1) {
        f5jBuf[charCnt++] = incomingByte;
      }
      else {
        if ((incomingByte == CR) && (charCnt == f5jExtendedProtoLen-1)) {
          // message complete.
// debug
  f5jBuf[f5jExtendedProtoLen] = 0;
  Serial.println(f5jBuf);
//          
          charCnt = 0xff;
          // check & extract paylode data
          if ((f5jBuf[0] != 'R') ||
              (f5jBuf[3] != 'G') ||
              (f5jBuf[6] != 'T') ||
              ((f5jBuf[12] != 'T')&&(f5jBuf[12] != 'F'))) {
            return false;
            }
          if ((f5jBuf[11] != 'P') &&
              (f5jBuf[11] != 'W') &&
              (f5jBuf[11] != 'L') &&
              (f5jBuf[11] != 'S') &&
              (f5jBuf[11] != 'D') &&
              (f5jBuf[11] != 'T') &&
              (f5jBuf[11] != 'N')) {
            return false;
            }
          if ((!isDigit(f5jBuf[1])) ||
              (!isDigit(f5jBuf[2])) ||
              (!isDigit(f5jBuf[4])) ||
              (!isDigit(f5jBuf[5])) ||
              (!isDigit(f5jBuf[7])) ||
              (!isDigit(f5jBuf[8])) ||
              (!isDigit(f5jBuf[9])) ||
              (!isDigit(f5jBuf[10]))) {
            return false;                
            }
// debug
          Serial.println("Message valid.");
//
          // valid extended protocol message received
          timeType = f5jBuf[11];  //P, W, L, S, D, T, N
          
          // copy payload (digits) info into dedicated buffers
          rBuf[0] = f5jBuf[1];
          rBuf[1] = f5jBuf[2];
          gBuf[0] = f5jBuf[4];
          gBuf[1] = f5jBuf[5];
          for (int i=0; i<4; i++)
            tBuf[i] = f5jBuf[7+i];     
          f5jTimerState = f5jBuf[11];
          charCnt++;   // invalidate all other incoming char
          // message complete.
 
          // prepare info based on timer state
          // Calculate the 2s windows for Round-Group display
          displayRoundGroup = false;
          if ((f5jTimerState == 'P') && (tBuf[1] >= '2') &&
             (((tBuf[2] == '0') && ((tBuf[3] == '0') || (tBuf[3] == '1'))) ||
              ((tBuf[2] == '1') && ((tBuf[3] == '5') || (tBuf[3] == '6'))) ||
              ((tBuf[2] == '3') && ((tBuf[3] == '0') || (tBuf[3] == '1'))) ||
              ((tBuf[2] == '4') && ((tBuf[3] == '5') || (tBuf[3] == '6'))))) 
              displayRoundGroup = true;

// no group/round display during working time anymore; ignore code snippet
//          if ((f5jTimerState == 'W') && ((tBuf[0] == '1') || ((tBuf[0] == '0') && (tBuf[1] >= '4'))) &&
//             (((tBuf[2] == '0') && ((tBuf[3] == '0') || (tBuf[3] == '1'))) ||
//              ((tBuf[2] == '1') && ((tBuf[3] == '5') || (tBuf[3] == '6'))) ||
//              ((tBuf[2] == '3') && ((tBuf[3] == '0') || (tBuf[3] == '1'))) ||
//              ((tBuf[2] == '4') && ((tBuf[3] == '5') || (tBuf[3] == '6'))))) 
//              displayRoundGroup = true;

          if (displayRoundGroup) {
            // fill display buffer with "RR-G" (G 0..9)
            buf[0] = rBuf[0];
            buf[1] = rBuf[1];
            buf[2] = '-';
            buf[3] = gBuf[1];
          }
          else {
            // fill display buffer with "mmss"
            for (int i=0; i<4; i++)
              buf[i] = tBuf[i];     
          }
// debug
          buf[4] = 0;
          Serial.println(buf);
//         
          return true;
        }
      }
    }
    if (f5jProtoMode == 2) {
       //ignore Pandora protocol
       return false;
    }
  }
  return false;
}

//
// standard protocol only; no longer used
//
//byte _updateSerialProtocolF5J(void) {
//  while (Serial.available()) {  
//    incomingByte = Serial.read();
//    // handle protocol
//    switch (incomingByte) {
//      case 'A':
//        // start of message
//        charCnt = 0;
//        for (int i=0; i<MaxDigits; i++) {
//          buf[i]=' ';
//        }
//      break;
//      default:
//        // collect message payload
//        if (charCnt < noOfDigits) {
//          if (isDigit(incomingByte)) {
//            buf[charCnt] = incomingByte;
//            charCnt++;
//          }
//          else {
//            // invalid char in message. Skip message.
//            charCnt = 0xff;
//          }
//          return false;
//        }
//        else {
//          if ((incomingByte == CR) && (charCnt == noOfDigits)) {
//             charCnt++;   // invalidate all other incoming char
//             // message complete.
//           return true;
//          }
//        }
//    }
//  }
//  return false;
//}

//
// Display timeout.
//
void displayTimeout(void) {
  //set display to '--------'
  digitalWrite(PIN_STORE, LOW);
  if (mode != 4) {
    for(byte i=0; i<MaxDigits; i++) 
      shiftOut(PIN_DATA, PIN_SHIFT, MSBFIRST, 0b01000000);
  } else {
      for(byte i=0; i<4; i++) 
        shiftOut(PIN_DATA, PIN_SHIFT, MSBFIRST, 0b01000000);
      for(byte i=4; i<MaxDigits; i++) 
        shiftOut(PIN_DATA, PIN_SHIFT, MSBFIRST, 0b00000000);
  }
  digitalWrite(PIN_STORE, HIGH);
}


void runDisplaySelfTest(void) {
byte debugDigit;
  digitalWrite(LED_BUILTIN, LOW);
  // display individual segments one by one, in all 7-segment displays at the same time
  for (byte j=0; j<MaxSegments; j++) {
    debugDigit = 0;
    debugDigit |= 1 << j;
    digitalWrite(PIN_STORE, LOW);
    for(byte i=0; i<MaxDigits; i++) 
        shiftOut(PIN_DATA, PIN_SHIFT, MSBFIRST, debugDigit);
    digitalWrite(PIN_STORE, HIGH);
    delay(200);
  }
  // display digits from 0 to 9 (one by one) in all 7-segment displays at the same time
  debugDigit = 0;
  for (byte j=0; j<sizeof(digits); j++) {
    digitalWrite(PIN_STORE, LOW);
    for(byte i=0; i<MaxDigits; i++) 
      shiftOut(PIN_DATA, PIN_SHIFT, MSBFIRST, digits[debugDigit]);
    digitalWrite(PIN_STORE, HIGH);
    debugDigit++;
    delay(200);
  }
 // blank all digits
  digitalWrite(PIN_STORE, LOW);
  for(byte i=0; i<MaxDigits; i++) 
    shiftOut(PIN_DATA, PIN_SHIFT, MSBFIRST, 0);
  digitalWrite(PIN_STORE, HIGH);
  delay(500);

  // display the number of each 7-segment display (1..8); in  mode 4: 1..4, 1..4
  debugDigit = 8;
  digitalWrite(PIN_STORE, LOW);
  if (mode != 4) {
    for(byte i=0; i<MaxDigits; i++) {
      shiftOut(PIN_DATA, PIN_SHIFT, MSBFIRST, digits[debugDigit--]);
    }
  } else {
    for (int j=0; j<2; j++) {
      debugDigit = 4;
      for(byte i=0; i<4; i++) {
        shiftOut(PIN_DATA, PIN_SHIFT, MSBFIRST, digits[debugDigit--]);
      }
    }
  }
  digitalWrite(PIN_STORE, HIGH);
  delay(2500);
  
  // blank all digits
  digitalWrite(PIN_STORE, LOW);
  for(byte i=0; i<MaxDigits; i++) 
    shiftOut(PIN_DATA, PIN_SHIFT, MSBFIRST, 0);
  digitalWrite(PIN_STORE, HIGH);
  delay(500);
}


void updateMode(void) {
  mode = getMode();
// debug
    mode = 4;
//
  if (mode != mode_old) {
    mode_old = mode;
    // initialize mode settings
    
    switch (mode) {
      case 7:     //F5B display with 7 digits (1..8) with legacy display control
      case 3:     //F5B display with 7 digits (1..8) with Marco special display control
        Serial.begin(9600);
        digitalWrite(PIN_G1, LOW);  // enable digit group 1 (digits 1..4)
        digitalWrite(PIN_G2, LOW);  // enable digit group 2 (digits 5..8)
        noOfDigits = NoOfDigitsF5B;
        delay(50);
        break;
      case 6:     // F5J display with 4 digits @9600bd, Standard “Embedded-Ability” or extended "R99G99TmmssAA+CR" format
        Serial.begin(9600);
        digitalWrite(PIN_G1, LOW);  // enable digit group 1 (digits 1..4)
        noOfDigits = NoOfDigitsF5J;
        delay(50);
      break;
      case 5:     // F5J display with 4 digits @19200bd, Standard “Embedded-Ability” or extended "R99G99TmmssAA+CR" format
        Serial.begin(19200);
        digitalWrite(PIN_G1, LOW);  // enable digit group 1 (digits 1..4)
        noOfDigits = NoOfDigitsF5J;
        delay(50);
      break;
      case 4:     // F5J display with 2x4 digits back to back @ 9600bd, Standard “Embedded-Ability” or extended "R99G99TmmssAA+CR" format
        Serial.begin(9600);
        digitalWrite(PIN_G1, LOW);  // enable digit group 1 (digits 1..4)
        digitalWrite(PIN_G2, LOW);  // enable digit group 2 (digits 5..8)
        noOfDigits = NoOfDigitsF5J;
        delay(50);
      break;
      case 2:     // t.b.d.
      break;
      case 1:     // t.b.d.
      break;
      case 0:     // t.b.d.
      break;
      default:
        digitalWrite(PIN_G1, HIGH);  // disable digit group 1 (digits 1..4)
        digitalWrite(PIN_G2, HIGH);  // disable digit group 2 (digits 5..8)
        noOfDigits = 0;
    }
  }
}




void setup() {
  // shift pins
  pinMode(PIN_STORE, OUTPUT);
  pinMode(PIN_SHIFT, OUTPUT);
  pinMode(PIN_DATA, OUTPUT);
  pinMode(PIN_nSRCLR, OUTPUT);
  pinMode(PIN_G1, OUTPUT);
  pinMode(PIN_G2, OUTPUT);
  // mode pins
  pinMode(PIN_MODE1, INPUT_PULLUP);
  pinMode(PIN_MODE2, INPUT_PULLUP);
  pinMode(PIN_MODE3, INPUT_PULLUP);
  // extra pin
  pinMode(PIN_MODE4, INPUT_PULLUP);
  // LED
  pinMode(LED_BUILTIN, OUTPUT);
  
  digitalWrite(PIN_G1, HIGH);
  digitalWrite(PIN_G2, HIGH);
  digitalWrite(PIN_nSRCLR, HIGH);
  digitalWrite(PIN_STORE, LOW);
  digitalWrite(PIN_SHIFT, LOW);
  digitalWrite(PIN_DATA, LOW);
  // clear serial shift register...
  digitalWrite(PIN_nSRCLR, LOW);
  delay(1);
  digitalWrite(PIN_nSRCLR, HIGH);
  // and fill register with all zeros
  digitalWrite(PIN_STORE, HIGH);
  delay(1);
  digitalWrite(PIN_STORE, LOW);

  digitalWrite(LED_BUILTIN, LOW);

  // clear ASCII buffer
  for(byte i=0; i<sizeof(buf); i++) {
    buf[i] = 0;
  }
  // get operational mode
  mode_old = 0xff;  // force mode initialization
  updateMode();
  // Run display test once
  runDisplaySelfTest();
  
// debug
  Serial.println("Initialized.");
  Serial.print("Mode: ");Serial.println(mode);
//
}



void loop () {
   
  // do main task, shift out string
  // check if self test requested (jumper on PIN_MODE4 set); else normal mode
  if (digitalRead(PIN_MODE4)) {
//  if (true) {
//    mode = 6;

    // normal mode
    switch (mode) {
      case 7:     // F5B
        if (updateSerialProtocolF5B()) {
          shiftOutNumberStringF5BLegacy(buf);
          watchdogCnt = millis() + WD_TIME;    // update watchdog
        }
       break;
       case 6:      // F5J @ 9600bd
       case 5:      // F5J @ 19200bd
       case 4:      // F5J @ 9600bd and 2x4 digits
        if (updateSerialProtocolF5J()) {
          if (mode == 4) {
            for (int i=0; i<noOfDigits; i++) {
              buf[i+noOfDigits] = buf[i]; // double digits in buffer for second light group
            }
// debug
  buf[MaxDigits] = 0;
  Serial.println(buf);
//
          }
          shiftOutNumberStringF5J(buf);
          watchdogCnt = millis() + WD_TIME;    // update watchdog
          currentTime = millis();
          if (currentTime - previousTime > 500) {
            flashPulse = !flashPulse;         // period 1s: on/off time 500ms
            previousTime = currentTime;
          }
        }
       break;
       case 3:     // Common F5B/F5J sign
        if (updateSerialProtocolF5B()) {
          shiftOutNumberStringF5BMarco(buf);
          watchdogCnt = millis() + WD_TIME;    // update watchdog
        }
       break;
   }
  
    // check for current mode setting
    updateMode();
    // check for communication timeout
    if (millis() > watchdogCnt) {
      // timed out, set display to '--------'
//      watchdogCnt = millis() + WD_TIME;
      timeout = true;
      lsTime = LS_TIME1;
    }
    else {
      timeout = false;
      lsTime = LS_TIME2;
    }

    // Toggle life sign LED
    if (millis() > (timecnt+lsTime)) {
      timecnt = millis();
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // toggle LED
      if (timeout)
        displayTimeout();
   }
  }
  else {
    // self test mode
    runDisplaySelfTest();
  }
}
