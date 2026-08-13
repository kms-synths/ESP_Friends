/*
    PMO-RP1 V2.0
    20200427_OLEDoscilloscope_V200E.ino)
    sketch：23020byte、local variable:1231byte free
    Apr.27 2020 by radiopench http://radiopench.blog96.fc2.com/

    PMO-RP/JSB V2.0s
    July 2020, simplified version by Zaphod B.
    This version is meant to display the signal only for visual inspection of
    the signal form, not for measurement purposes. Consequently this version:
    - has no y-axis annotations,
    - does not show the duty cycle,
    - has no ticks on the x-axis,
    - uses the full width of the display to show the signal.

    PMO-RP/JSB V2.1
    April 14. 2021,
    - added vertical offset levelling menu item.
    - Led will signal when data has been saved to EEPROM via pulsed flashing.

    PMO-RP/JSB V2.2
    May 7. 2021,
    - storing vertical offset for each range to EEPROM
    - voltage range menu now wraps around

    PMO-RP/JSB V3.0
    a.k.a.
    SPIZDILI -- kmsscope v1.0
    19.07.2026 (because DD/MM/YYYY is superior),
    - full rework to adapt it to ESP32C3
    - reworked to Eurorack realities, e.g. two modes for DC coupled signals (LFOs, ADSRs, CVs) and two modes for bipolar (preferably AC coupled) signals (basically   any audio signals)
   - software calibration for 146% precise values!1!!11!!
   - spectrum analyzer
   - RTFM -- calibration nor spectrum analyzer WILL NOT WORK PROPERLY without schematic!!!!!!!!!!!!!!!!!!!!!!!!
*/

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <Wire.h>
#include <arduinoFFT.h>

#define PIN_ADC 0
#define BTN_SEL 1
#define BTN_UP 2
#define BTN_DOWN 3
#define BTN_HOLD 4
#define PIN_SDA 6
#define PIN_SCL 7
#define PIN_LED 8

// Settings / appearance

#define BEGIN_X 0 // Begin position of plot on x-axis

#define OPTIONAL_SETTINGS // move settings between #ifdef and #else for enabling or disabling them

#ifdef OPTIONAL_SETTINGS
#define DISPLAY_ZERO_LINE 1    // Center line for Y-axis, like 1V65 for 3V3 modes
#define DISPLAY_CENTER_VALUE 1 // Voltage at the center line on Y-axis, like 0V, 1V65, 2V5, 4V5, etc.
#else
#define DISPLAY_VERTICAL_LINES 1 // 4 vertical lines, cutting the graph into 5.
#define DISPLAY_VERTICAL_MARKS 1 // Horizontal dotted lines at the bottom and the top of the graph. Goes well with DISPLAY_VERTICAL_LINES
#define DISPLAY_AVERAGE_TR 1     // Display average voltage in the top right corner
#define DISPLAY_AVERAGE_TL 1     // Display average voltage in the top left corner
#define DISPLAY_CENTER_VALUE 1   // Vref for Y-axis. Goes well with DISPLAY_ZERO_LINE
#define DISPLAY_DUTY_CYCLE 1     // Duty cycle for PWM signals
#define FREQ_Y 12                // freq text vertical position
#define DISPLAY_FREQUENCY        // Display freq
#define SHOW_OFFSET              // Display offset value (which is set by 'Zero') or not. Almost useless in this version.
#endif

#define SCOPE_P_UPPER 3 // 4 menu items
#define FREQ_X 90       // horizontal position of plotted frequency in pixels

#define INVERTED // inverted input in case you're using mine schematic with inverting op-amp

// System info

#define SCREEN_WIDTH 128 // OLED display width
#define SCREEN_HEIGHT 64 // OLED display height
#define REC_LENG 256     // size of wave data buffer
#define MIN_TRIG_SWING 5 // minimum trigger swing. "Unsync" if swing smaller than this value

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1                                                  // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // device name is oled

Preferences prefs; // eeprom

// FFT

#define FFT_SAMPLES 256
#define SAMPLING_FREQ 37000
double vReal[FFT_SAMPLES];
double vImag[FFT_SAMPLES];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SAMPLES, SAMPLING_FREQ);

// ADC

#define ADC_BASE_RES 4095 // ESP32 tastes better than arduino, partically because of 12bit ADC.
// But the ADC on ESP is ass: the only sensible range is 0-2.65V! => the input signal is 2.6Vpp, swinging around 1.3V.
uint16_t ADC_TRUE_RES = 4095; // ADC values swing between ADC_NEG and ADC_BASE_RES - ADC_POS
uint16_t ADC_NEG = 0;         // ADC Negative OffSet (offset from the minimum end, e.g. if POS == 0 and NOS == 15 => ADC_TRUE_RES starts from 14 and goes to 4095, aka == 4080)
uint16_t ADC_POS = 0;         // ADC Postive OffSet  (offset from the maximim end, e.g. if POS == 84 and NOS == 0 => ADC_TRUE_RES starts from  0 and goes to 4011, aka == 4011)
uint16_t ADC_0V = 2048;       // ADC value for displayed 0V (Vbias on the schematic, roughly 1.3V) after calibration

// Range name table (those are stored in flash memory)
#define VRANGE_MAX 5
const char vRangeName[5][5] = {"Auto", "24V", "10V", " 5V ", "3V3"}; // Vertical display character (number of characters including \ 0 is required)
const char *const vstring_table[] PROGMEM = {
    vRangeName[0],
    vRangeName[1],
    vRangeName[2],
    vRangeName[3],
    vRangeName[4]};
const char hRangeName[10][6] PROGMEM = {"200ms", "100ms", " 50ms", " 20ms", " 10ms", "  5ms", "  2ms", "  1ms", "500us", "  Min"}; //  Horizontal display characters
const char *const hstring_table[] PROGMEM = {
    hRangeName[0], hRangeName[1], hRangeName[2], hRangeName[3], hRangeName[4],
    hRangeName[5], hRangeName[6], hRangeName[7], hRangeName[8], hRangeName[9]};
const PROGMEM float hRangeValue[] = {0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001, 0.5e-3, 0.2e-3}; // horizontal range value in second. ( = 25pix on screen)

uint8_t dataOffset[VRANGE_MAX]; // Vertical offset for wave form.
int waveBuff[REC_LENG];         // Wave form buffer (RAM remaining capacity is barely)
char chrBuff[8];                // Display string buffer
char hScale[] = "xxxAs";        // Horizontal scale character
char vScale[] = "xxxx";         // Vertical scale

const float lsb50V = 0.05243212; // Sensivity coefficient of 50V range. std=0.0512898 1.1*520.91/(1024*10.91)

volatile int8_t vRange;                    // V-range number 0: Auto,  1:24V,   2:10V,  3:  5V, 4: 3V3
volatile int8_t hRange;                    // H-range number 0: 200ms, 1:100ms, 2:50ms, 3:20ms, 4:10ms, 5:5ms, 6:2ms, 7:1ms, 8:500us, 9: Min (going as fast as MCU can)
volatile boolean trigD = false;            // trigger slope flag,     false:positive true:negative
volatile uint8_t scopeP;                   // Operation scope position number. 0:Vertical, 1:Horizontal, 2:Trigger slope
volatile boolean hold = false;             // Hold flag
volatile boolean switchPushed = false;     // flag of switch pushed !
volatile boolean startCalibration = false; // flag of SEL+HOLD pushed
volatile boolean specanalyzer = false;     // flag for spectrum analyzer mode enabled

int8_t lastVRange = -1;
int8_t lastHRange = -1;
boolean lastTrigD = false;

uint16_t dataMin; // Buffer minimum value (smallest = 0)
uint16_t dataMax; //        maximum value (largest  = ADC_BASE_RES)
int dataAve;      // 10 x average value (use 10x value to keep accuracy. so, max=10230) // deprecated i guess???? or not??? anyway, max should be 10 x (ADC_BASE_RES - ADC_POS)
int rangeMax;     // Buffer value to graph full swing
int rangeMin;     // Buffer value of graph botto
int rangeMaxDisp; // Display value of max. (100x value)
int rangeMinDisp; // Display value if min.
int trigP;        // Trigger position pointer on data buffer
boolean trigSync; // Flag of trigger detected

float waveFreq; // Frequency (Hz)
float waveDuty; // Duty ratio (%)

void setConditions() { // measuring condition setting
    // get range name from memory
    strcpy(hScale, hstring_table[hRange]);
    strcpy(vScale, vstring_table[vRange]);

    switch (vRange) {
    case 0: { // Auto
        break;
    }
    case 1: { // 24V
        rangeMin = ADC_NEG;
        rangeMax = ADC_BASE_RES - ADC_POS;
        rangeMinDisp = -1200; // -12.0V
        rangeMaxDisp = +1200; // +12.0V
        break;
    }
    case 2: { // 10V
        rangeMin = ADC_0V - ADC_TRUE_RES * 5 / 24.0;
        rangeMax = ADC_0V + ADC_TRUE_RES * 5 / 24.0;
        rangeMaxDisp = 500;  // +5.0V
        rangeMinDisp = -500; // -5.0V2
        break;
    }
    case 3: { // 5V
        rangeMin = ADC_0V;
        rangeMax = ADC_0V + ADC_TRUE_RES * 5 / 24.0;
        rangeMaxDisp = 500; // 5.00V
        rangeMinDisp = 0;
        break;
    }
    case 4: { // 3V3
        rangeMin = ADC_0V;
        rangeMax = ADC_0V + ADC_TRUE_RES * 3.3 / 24.0;
        rangeMaxDisp = 330; // 3.30V
        rangeMinDisp = 0;
        break;
    }
    }
}

void writeCommonImage() {     // Show menu.
    oled.clearDisplay();      // Erase all
    oled.setTextColor(WHITE); // Use white characters

#ifdef DISPLAY_AVERAGE_TR
    oled.setCursor(86, 0);      // Start at top-left corner
    oled.println(F("av    V")); // 1-st line fixed characters
#endif

#ifdef DISPLAY_VERTICAL_LINE_LEFT
    oled.drawFastVLine(BEGIN_X + 2, 9, 55, WHITE); // left vertical line
#endif

#ifdef DISPLAY_VERTICAL_MARKS
    oled.drawFastVLine(SCREEN_WIDTH - 1, 9, 3, WHITE); // right vertical line up
    oled.drawFastVLine(SCREEN_WIDTH - 1, 61, 3,
                       WHITE); // right vertical line bottom

    oled.drawFastHLine(BEGIN_X, 9, 7, WHITE); // Max value auxiliary mark
    oled.drawFastHLine(BEGIN_X, 36, 2, WHITE);
    oled.drawFastHLine(BEGIN_X, 63, 7, WHITE);

    oled.drawFastHLine(BEGIN_X + 27, 9, 3, WHITE); // Max value auxiliary mark.
    oled.drawFastHLine(BEGIN_X + 27, 63, 3, WHITE);

    oled.drawFastHLine(BEGIN_X + 52, 9, 3, WHITE); // Max value auxiliary mark.
    oled.drawFastHLine(BEGIN_X + 52, 63, 3, WHITE);

    oled.drawFastHLine(BEGIN_X + 77, 9, 3, WHITE); // Max value auxiliary mark.
    oled.drawFastHLine(BEGIN_X + 77, 63, 3, WHITE);

    oled.drawFastHLine(BEGIN_X + 99, 9, 5,
                       WHITE); // Right side Max value auxiliary mark.
    oled.drawFastHLine(BEGIN_X + 99, 63, 5, WHITE);
#endif

#ifdef DISPLAY_ZERO_LINE
    // for (int x = BEGIN_X + 2; x <= SCREEN_WIDTH; x += 5) {
    // There are 128 samples that are displayed.
    // So we need a zero line of that same length.
    for (int x = BEGIN_X + 2; x <= SCREEN_WIDTH - 1 + BEGIN_X + 2; x += 5) {
        oled.drawFastHLine(x, 36, 2, WHITE); // Draw the center line (horizontal line) with a dotted line.
    }
#endif

#ifdef DISPLAY_VERTICAL_LINES
    for (int x = (SCREEN_WIDTH - 1 - 25); x > 6; x -= 25) {
        for (int y = 10; y < 63; y += 5) {
            oled.drawFastVLine(x, y, 2, WHITE); // Draw 3 vertical lines with dotted lines.
        }
    }
#endif
}

void readWave() {
    switchPushed = false;
    unsigned long sampleInterval = 0;

    // REC_LENG = 200 -> need to sample 200, well, samples per value -> sample
    // interval should be [hRange * 1000 / REC_LENG] (*1000 since program uses microseconds).
    // I really hope nothing will be screwed up by adjusting wave buffer to 256...
    switch (hRange) {
    case 0:
        sampleInterval = 1000;
        break; // 200ms
    case 1:
        sampleInterval = 500;
        break; // 100ms
    case 2:
        sampleInterval = 250;
        break; // 50ms
    case 3:
        sampleInterval = 100;
        break; // 20ms
    case 4:
        sampleInterval = 50;
        break; // 10ms
    case 5:
        sampleInterval = 25;
        break; // 5ms
    case 6:
        sampleInterval = 10;
        break; // 2ms
    case 7:
        sampleInterval = 5;
        break; // 1ms
    case 8:
        sampleInterval = 2;
        break; // 500us (almost ADC limit)
    case 9:
        sampleInterval = 0;
        break; // Maximum speed
    }

    for (int i = 0; i < REC_LENG; i++) {
        unsigned long startTime = micros();
#ifndef INVERTED
        waveBuff[i] = analogRead(PIN_ADC);
#else
        waveBuff[i] = ADC_BASE_RES - analogRead(PIN_ADC);
#endif
        if (sampleInterval > 0) {
            while (micros() - startTime < sampleInterval) {
            }
        }
        if (switchPushed == true) break;
    }
}

int sum3(int k) { // Sum of before and after and own value
    int m = waveBuff[k - 1] + waveBuff[k] + waveBuff[k + 1];
    return m;
}

void freqDuty() {       // Detect frequency and duty cycle value from waveform data
    int swingCenter;    // Center of wave (half of p-p)
    float p0 = 0;       // 1-st posi edge
    float p1 = 0;       // Total length of cycles
    float p2 = 0;       // Total length of pulse high time
    float pFine = 0;    // Fine position (0-1.0)
    float lastPosiEdge; // Last positive edge position

    float pPeriod; // Pulse period
    float pWidth;  // Pulse width

    int p1Count = 0; // Wave cycle count
    int p2Count = 0; // High time count

    boolean a0Detected = false;
    boolean posiSearch = true; // True when searching posi edge

    swingCenter = (3 * (dataMin + dataMax)) / 2; // Calculate wave center value

    for (int i = 1; i < REC_LENG - 2; i++) {                                                                      // Scan all over the buffer
        if (posiSearch == true) {                                                                                 // Positive slope (frequency search)
            if ((sum3(i) <= swingCenter) && (sum3(i + 1) > swingCenter)) {                                        // if across the center when rising (+-3data used to eliminate noize)
                pFine = (float)(swingCenter - sum3(i)) / ((swingCenter - sum3(i)) + (sum3(i + 1) - swingCenter)); // fine cross point calc.
                if (a0Detected == false) {                                                                        // If 1-st cross
                    a0Detected = true;                                                                            // Set find flag
                    p0 = i + pFine;                                                                               // Save this position as startposition
                } else {
                    p1 = i + pFine - p0; // Record length (length of n*cycle time)
                    p1Count++;
                }
                lastPosiEdge = i + pFine; // Record location for Pw calcration
                posiSearch = false;
            }
        } else {                                                           // Negative slope search (duration saerch)
            if ((sum3(i) >= swingCenter) && (sum3(i + 1) < swingCenter)) { // if across the center when falling (+-3data used to eliminate noize)
                pFine = (float)(sum3(i) - swingCenter) / ((sum3(i) - swingCenter) + (swingCenter - sum3(i + 1)));
                if (a0Detected == true) {
                    p2 = p2 + (i + pFine - lastPosiEdge); // calculate pulse width and accumurate it
                    p2Count++;
                }
                posiSearch = true;
            }
        }
    }

    pPeriod = p1 / p1Count; // pulse period
    pWidth = p2 / p2Count;  // palse width

    waveFreq = 1.0 / ((pgm_read_float(hRangeValue + hRange) * pPeriod) / 25.0); // frequency
    waveDuty = 100.0 * pWidth / pPeriod;                                        // duty ratio
}

void dataAnalyze() { // get various information from wave form
    int d;
    long sum = 0;

    // search max and min value
    dataMin = ADC_BASE_RES - ADC_POS;    // min value initialize to big number
    dataMax = ADC_NEG;                   // max value initialize to small number
    for (int i = 0; i < REC_LENG; i++) { // serach max min value
        d = waveBuff[i];
        sum = sum + d;
        if (d < dataMin) dataMin = d; // update min
        if (d > dataMax) dataMax = d; // updata max
    }

    // calculate average
    dataAve = (sum + 10) / 20; // Average value calculation (calculated by 10 times to improve accuracy)

    // Decide display's max min value
    if (vRange == 0) {                         // if Autorange (Range number = 0）          // rev. krezhvick: original PMO used 0 and 1 as A5V and A50V and att10x variable. the code below was severely edited from the original to remove not-used variables since I made one 'Auto' mode
        rangeMin = dataMin - 20;               // maintain bottom margin 20
        rangeMin = (rangeMin / 10) * 10;       // round 10
        if (rangeMin < 0) rangeMin = 0;        // no smaller than 0
        rangeMax = dataMax + 20;               // Set display top at  data max +20
        rangeMax = ((rangeMax / 10) + 1) * 10; // Round up 10

        rangeMaxDisp = 100 * (rangeMax * lsb50V); // display range is determined by the data.(the upper limit is up to the full scale of the ADC)
        rangeMinDisp = 100 * (rangeMin * lsb50V); // lower depend on data, but zero or more
    }

    // Trigger position search
    for (trigP = ((REC_LENG / 2) - 51); trigP < ((REC_LENG / 2) + 50); trigP++) {                                       // Find the points that straddle the median at the center ± 50 of the data range
        if (trigD == false) {                                                                                           // if trigger direction is positive
            if ((waveBuff[trigP - 1] < (dataMax + dataMin) / 2) && (waveBuff[trigP] >= (dataMax + dataMin) / 2)) break; // Positive trigger position found!
        } else {                                                                                                        // Trigger direction is negative.
            if ((waveBuff[trigP - 1] > (dataMax + dataMin) / 2) && (waveBuff[trigP] <= (dataMax + dataMin) / 2)) break; // Negative trigger position found!
        }
    }

    trigSync = true;
    if (trigP >= ((REC_LENG / 2) + 50)) { // If the trigger is not found in range
        trigP = (REC_LENG / 2);           // Set it to the center for the time being
        trigSync = false;                 // Set Unsync display flag.
    }
    if ((dataMax - dataMin) <= MIN_TRIG_SWING) trigSync = false; // Amplitude of the waveform smaller than the specified value => set Unsync display flag.
    freqDuty();
}

void startScreen() { // Start up screen
    oled.clearDisplay();
    oled.setTextSize(2); // at double size character
    oled.setTextColor(WHITE);
    oled.setCursor(20, 25);
    oled.println(F("kms")); // Title (it was Poor Man's Osilloscope, RadioPench 1, but you really can change it to whatever. i just used my nick)
    oled.setCursor(20, 35);
    oled.println(F("  scope"));
    oled.display(); // Actual display here.
    delay(1500);
    oled.clearDisplay();
    oled.setTextSize(1); // After this, standard font size.
}

void dispHold() {                        // Display "Hold"
    oled.fillRect(42, 11, 24, 8, BLACK); // Black paint 4 characters.
    oled.setCursor(42, 11);
    oled.print(F("Hold"));
    oled.display();
}

void dispInf() { // Display of menu
    float voltage;

    // Display vertical sensitivity.
    oled.setCursor(2, 0);                    // Around top left
    oled.print(vScale);                      // Vertical sensitivity value
    if (scopeP == 0) {                       // if scoped then
        oled.drawFastHLine(0, 7, 27, WHITE); // display scoped mark at the bottom.
        oled.drawFastVLine(0, 5, 2, WHITE);
        oled.drawFastVLine(26, 5, 2, WHITE);
    }

    // Display horizontal sweep speed.
    oled.setCursor(34, 0);
    oled.print(hScale);                       // Display sweep speed (time/div).
    if (scopeP == 1) {                        // If scoped then
        oled.drawFastHLine(32, 7, 33, WHITE); // display scoped mark at the bottom.
        oled.drawFastVLine(32, 5, 2, WHITE);
        oled.drawFastVLine(64, 5, 2, WHITE);
    }

    // Display trigger polarity.
    oled.setCursor(75, 0); // At top center
    if (trigD == false)
        oled.print(char(0x18)); // if positive then show up mark,
    else
        oled.print(char(0x19)); // else show down mark.

    if (scopeP == 2) {                        // If scoped then
        oled.drawFastHLine(71, 7, 13, WHITE); // display scoped mark at the bottom.
        oled.drawFastVLine(71, 5, 2, WHITE);
        oled.drawFastVLine(83, 5, 2, WHITE);
    }

    // Show calibration menu item.
    oled.setCursor(94, 0); // Top right
    if (specanalyzer == false)
        oled.print(F("Scope"));
    else
        oled.print(F("Spect"));

    if (scopeP == 3) {
        oled.drawFastHLine(92, 7, 34, WHITE); // Display zero menu item mark.
        oled.drawFastVLine(92, 5, 2, WHITE);
        oled.drawFastVLine(125, 5, 2, WHITE);
    }

#ifdef DISPLAY_AVERAGE_TR
    // Average voltage top right
    // if 10x attenuator is used  // rev. krezhvick: yet again, I deleted the difference between att10x and att1x
    voltage = dataAve * lsb50V / 10.0;   // 50V range value
    if (voltage < 10.0) {                // if less than 10V
        dtostrf(voltage, 4, 2, chrBuff); // format x.xx
    } else {                             // no!
        dtostrf(voltage, 4, 1, chrBuff); // format xx.x
    }
    oled.setCursor(98, 0); // around the top right
    oled.print(chrBuff);   // display average voltage
#endif

#ifdef DISPLAY_AVERAGE_TL
    // average voltage top left
    // vertical scale lines
    voltage = rangeMaxDisp / 100.0;      // convert Max voltage
    if (vRange == 4) {                   // if range below 5V
        dtostrf(voltage, 4, 2, chrBuff); // format *.**
    } else {                             // no!
        dtostrf(voltage, 4, 1, chrBuff); // format **.*
    }
    oled.setCursor(0, 9);
    oled.print(chrBuff); // display Max value
#endif

#ifdef DISPLAY_CENTER_VALUE
    voltage = (rangeMaxDisp + rangeMinDisp) / 200.0; // center value calculation
    if (vRange == 4) {                               // if range below 5V
        dtostrf(voltage, 4, 2, chrBuff);             // format *.**
    } else {                                         // no!
        dtostrf(voltage, 4, 1, chrBuff);             // format **.*
    }
    oled.setCursor(0, 33);
    oled.print(chrBuff); // display the value
#endif

#ifdef DISPLAY_MIN
    // Bottom left
    voltage = rangeMinDisp / 100.0;      // convert Min voltage
    if (vRange == 4) {                   // if range below 5V
        dtostrf(voltage, 4, 2, chrBuff); // format *.**
    } else {                             // no!
        dtostrf(voltage, 4, 1, chrBuff); // format **.*
    }
    oled.setCursor(0, 57);
    oled.print(chrBuff); // display the value
#endif

    // Display frequency, duty % or trigger missed.
#ifdef DISPLAY_FREQUENCY
    if (trigSync == false) { // If trigger point can't found
        oled.fillRect(FREQ_X + 1, FREQ_Y + 2, 24, 8,
                      BLACK);                   // black paint 4 character
        oled.setCursor(FREQ_X + 1, FREQ_Y + 2); //
        oled.print(F("noisy"));                 // display Unsync
    } else {
        oled.fillRect(FREQ_X, FREQ_Y, 25, 9, BLACK); // erase Freq area
        oled.setCursor(FREQ_X + 1, FREQ_Y + 1);      // set display locatio
        if (waveFreq < 100.0) {                      // if less than 100Hz
            oled.print(waveFreq, 1);                 // display 99.9Hz
            oled.print(F("Hz"));
        } else if (waveFreq < 1000.0) { // if less than 1000Hz
            oled.print(waveFreq, 0);    // display 999Hz
            oled.print(F("Hz"));
        } else if (waveFreq < 10000.0) {        // if less than 10kHz
            oled.print((waveFreq / 1000.0), 2); // display 9.99kH
            oled.print(F("kH"));
        } else {                                // if more
            oled.print((waveFreq / 1000.0), 1); // display 99.9kH
            oled.print(F("kH"));
        }
#ifdef DISPLAY_DUTY_CYCLE
        oled.fillRect(FREQ_X + 6, FREQ_Y + 9, 25, 10,
                      BLACK);                    // Erase Freq area (as small as possible).
        oled.setCursor(FREQ_X + 7, FREQ_Y + 11); // Set location.
        oled.print(waveDuty, 1);                 // Sisplay duty (High level ratio) in %.
        oled.print(F("%"));
#endif
    }
#endif
}

void plotData() { // Plot wave form on OLED over full screen width.
    long y1, y2;
    long currentOffset = (vRange == 0) ? dataOffset[0] : 0;

    for (int x = 0; x <= 98; x++) {
        y1 = map(waveBuff[x + trigP - 50] + currentOffset, rangeMin, rangeMax, 63, 9);
        // y1 = constrain(y1, 9, 63);

        y2 = map(waveBuff[x + trigP - 49] + currentOffset, rangeMin, rangeMax, 63, 9);
        // y2 = constrain(y2, 9, 63);

        int xx = map(x, 0, 98, 0, SCREEN_WIDTH - 1);
        oled.drawLine(xx + BEGIN_X + 3, y1, xx + BEGIN_X + 4, y2, WHITE);
    }
}

void plotSpectrum() { // Plot audio-spectrum at the same space, as the scope
    for (uint16_t i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] = waveBuff[i] - ADC_0V;
        vImag[i] = 0.0;
    }

    FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.compute(FFT_FORWARD);
    FFT.complexToMagnitude();

    for (uint16_t i = 0; i < 128; i++) {
        double mag = vReal[i];
        double db = 20.0 * log10(mag + 1.0);
        long y = map((long)db, 50, 100, 63, 9);
        y = constrain(y, 9, 64);
        oled.drawFastVLine(i, y, 63 - y + 1, WHITE);
    }
}

void calibrateADC(int8_t s) {
    long x = 0;

    oled.clearDisplay();
    oled.setTextColor(WHITE);
    oled.setCursor(30, 16);
    oled.println(F("Calibration"));
    oled.setCursor(20, 30);
    if (s == -1)
        oled.println(F("Connect to -12V"));
    else if (s == 0)
        oled.println(F("Connect to GND"));
    else
        oled.println(F("Connect to +12V"));

    oled.setCursor(30, 40);
    oled.println(F("and press OK"));

    oled.display();

    while (digitalRead(BTN_SEL) == HIGH)
        delay(10);

    for (int i = 0; i < 100; i++) {
        x = x + analogRead(PIN_ADC);
        delay(1);
    }

    oled.clearDisplay();
    oled.setCursor(20, 16);
    oled.println(F("Calibration"));
    oled.setCursor(20, 30);

    if (s == -1) {
        ADC_NEG = ADC_BASE_RES - (x / 100.0);
        oled.print(F("ADC_NEG = "));
        oled.println(ADC_NEG);
    } else if (s == 0) {
#ifndef INVERTED
        ADC_0V = (x / 100.0);
#else
        ADC_0V = ADC_BASE_RES - (x / 100.0);
#endif
        oled.print(F("ADC_0V = "));
        oled.println(ADC_0V);
    } else {
        ADC_POS = (x / 100.0);
        oled.print(F("ADC_POS = "));
        oled.println(ADC_POS);
    }

    oled.display();
    while (digitalRead(BTN_SEL) == LOW)
        delay(10);

    delay(2000);
}

void IRAM_ATTR pin2IRQ() {
    switchPushed = true;

    if (digitalRead(BTN_HOLD) == LOW && digitalRead(BTN_SEL) == LOW) {
        startCalibration = true;
        return;
    }

    if (digitalRead(BTN_SEL) == LOW) {
        scopeP = scopeP + 1;
        if (scopeP > SCOPE_P_UPPER) scopeP = 0;
    }

    if (digitalRead(BTN_UP) == LOW) {
        switch (scopeP) {
        case 0:
            vRange = vRange + 1;
            if (vRange > 4) vRange = 0;
            break;
        case 1:
            hRange = hRange + 1;
            if (hRange > 9) hRange = 9;
            break;
        case 2:
            trigD = false;
            break;
        case 3:
            specanalyzer = true;
            break;
        }
    }

    if (digitalRead(BTN_DOWN) == LOW) {
        switch (scopeP) {
        case 0:
            vRange = vRange - 1;
            if (vRange < 0) vRange = 4;
            break;
        case 1:
            hRange = hRange - 1;
            if (hRange < 0) hRange = 0;
            break;
        case 2:
            trigD = true;
            break;
        case 3:
            specanalyzer = false;
            break;
        }
    }

    if (digitalRead(BTN_HOLD) == LOW) hold = !hold;
}

// EEPROM

void loadSettings() {
    prefs.begin("settings", true);
    vRange = prefs.getInt("vrange", 0);
    hRange = prefs.getInt("hrange", 0);
    trigD = prefs.getBool("trigd", false);
    prefs.end();
}

void saveSettings() {
    prefs.begin("settings", false);
    prefs.putInt("vrange", vRange);
    prefs.putInt("hrange", hRange);
    prefs.putBool("trigd", trigD);
    prefs.end();
}

void loadCalibration() {
    prefs.begin("adccal", true);
    ADC_POS = prefs.getUInt("adc_pos", 0);
    ADC_NEG = prefs.getUInt("adc_neg", 0);
    ADC_0V = prefs.getUInt("adc_0v", 2048);
    prefs.end();

    ADC_TRUE_RES = ADC_BASE_RES - ADC_POS - ADC_NEG; // TRUE_RES calculeated here the same as on start
    // ADC_0V = ADC_NEG + (ADC_TRUE_RES) / 2;
}

void saveCalibration() {
    prefs.begin("adccal", false);
    prefs.putUInt("adc_pos", ADC_POS);
    prefs.putUInt("adc_neg", ADC_NEG);
    prefs.putUInt("adc_0v", ADC_0V);
    prefs.end();

    ADC_TRUE_RES = ADC_BASE_RES - ADC_POS - ADC_NEG; // TRUE_RES recalculation for current session
    // ADC_0V = ADC_NEG + (ADC_TRUE_RES) / 2;
}

// Setup + loop

void setup() {
    pinMode(BTN_SEL, INPUT_PULLUP);
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_HOLD, INPUT_PULLUP);
    pinMode(PIN_LED, OUTPUT); // Пин светодиода

    attachInterrupt(BTN_SEL, pin2IRQ, FALLING);
    attachInterrupt(BTN_UP, pin2IRQ, FALLING);
    attachInterrupt(BTN_DOWN, pin2IRQ, FALLING);
    attachInterrupt(BTN_HOLD, pin2IRQ, FALLING);

    Wire.begin(PIN_SDA, PIN_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) /* select 3C or 3D (set your OLED I2C address)*/
        for (;;)
            ;
    analogReadResolution(12);
    loadSettings();
    loadCalibration();
    for (int i = 1; i <= 4; i++)
        dataOffset[i] = 0;
    attachInterrupt(0, pin2IRQ, FALLING); // Activate IRQ at falling edge mode
    startScreen();                        // Display start message
}

void loop() {
    if (startCalibration) {
        startCalibration = false;
        detachInterrupt(digitalPinToInterrupt(BTN_SEL));
        calibrateADC(-1);
        calibrateADC(0);
        calibrateADC(1);
        saveCalibration();
        attachInterrupt(digitalPinToInterrupt(BTN_SEL), pin2IRQ, FALLING);
    }
    if (vRange != lastVRange || hRange != lastHRange || trigD != lastTrigD) {
        saveSettings();
        lastVRange = vRange;
        lastHRange = hRange;
        lastTrigD = trigD;
    }

    setConditions();             // Set measurement conditions
    digitalWrite(PIN_LED, HIGH); // Flash LED at start of measurement.
    readWave();                  // Read wave form and store into buffer memory.
    digitalWrite(PIN_LED, LOW);  // Stop LED at end of measurement.
    setConditions();             // Set measurment conditions again (reflect change during measure).
    dataAnalyze();               // Analyze data.
    writeCommonImage();          // Write fixed screen image (2.6ms).

    if (!specanalyzer)
        plotData(); // Plot waveform (10-18ms).
    else
        plotSpectrum();
    dispInf();      // Display information (6.5-8.5ms).
    oled.display(); // Send screen buffer to OLED (37ms).

    while (hold == true) {
        dispHold();
        delay(10);
    }
}
