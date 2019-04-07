#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

#define DIAL_R_PIN 27
#define DIAL_L_PIN 33
#define BUTTON_PIN 15
#define PIXEL_PIN 12
#define PIXEL_COUNT 30

TaskHandle_t RotaryEncoderTask; // handle to the task running on core 0
TaskHandle_t RunNeopixelsTask; // handle to the task running on core 1

Adafruit_NeoPixel strip = Adafruit_NeoPixel(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
enum COLORS_T { R, G, B, RG, RB, GB, LIGHT_G, LIGHT_B, LIGHT_RB, RGB, BLACK };
uint32_t colorMap[20];
int brightness = 50;

/*-********************** MEMORY SHARING VARS **********************/
bool readReady = false;
int incDecValue = 0;
bool incDecValid = false;
bool hasRead = false;

void setup()
{
	Serial.begin(115200);
	while (!Serial) { }
//	Serial.print("setup() running on core ");
//	Serial.println(xPortGetCoreID());

	pinMode(DIAL_L_PIN, INPUT);
	pinMode(DIAL_R_PIN, INPUT);
	pinMode(BUTTON_PIN, INPUT);
	pinMode(PIXEL_PIN,  OUTPUT);
	
	strip.setBrightness(brightness);
	strip.begin();
	show(); // Initialize all pixels to 'off'

	colorMap[(int)R] =        0xFA0000;
	colorMap[(int)G] =        0x00FA00;
	colorMap[(int)B] =        0x0000FA;
	colorMap[(int)RG] =       0xFAFA00;
	colorMap[(int)RB] =       0xFA00FA;
	colorMap[(int)GB] =       0x00FAFA;
	colorMap[(int)LIGHT_G] =  0x66FA44;
	colorMap[(int)LIGHT_B] =  0x7777FA;
	colorMap[(int)LIGHT_RB] = 0xBB66BB;
	colorMap[(int)RGB] =      0xFAFAFA;
	colorMap[(int)BLACK] =    0x000000;
	
	xTaskCreatePinnedToCore(
			ReadRotaryEncoder,   /* Task function. */
			"ReadRotaryEncoder", /* name of task. */
			10000,               /* Stack size of task */
			NULL,                /* parameter of the task */
			1,                   /* priority of the task */
			&RotaryEncoderTask,  /* Task handle to keep track of created task */
			0);                  /* pin task to core 0 ("ReadRotaryEncoder" code will run on core 0) */
	delay(500);
	
	xTaskCreatePinnedToCore(
			RunNeopixels,        /* Task function. */
			"RunNeopixels",      /* name of task. */
			10000,               /* Stack size of task */
			NULL,                /* parameter of the task */
			1,                   /* priority of the task */
			&RunNeopixelsTask,   /* Task handle to keep track of created task */
			1);                  /* pin task to core 1 ("RunNeopixels" code will run on core 1) */
	delay(500);
}

void loop()
{
	// let the watchdog know that everything is still ok
	TIMERG0.wdt_wprotect=TIMG_WDT_WKEY_VALUE;
	TIMERG0.wdt_feed=1;
	TIMERG0.wdt_wprotect=0;

	delay(10);
}

void show()
{
	// this is here because there is a bug with the neopixel library for the ESP32, specifically
	// https://github.com/adafruit/Adafruit_NeoPixel/issues/139
	delay(1);
	strip.show();
}






/*-*****************************************************************/
/*-*********************** NEOPIXEL VIS CODE ***********************/
/*-*****************************************************************/







enum VIS_T { COLORS, COLORS_RAND, RAIN, FADE, FADE_RAND, RAINBOW };
enum BUTTON_T {
	// The button is not being pressed
	BTN_OFF,
	// The button is currently being pressed and the dial hasn't been rotated since it's been pressed
	BTN_ON,
	// The button is currently being pressed, but the dial is also being rotated
	BTN_ON_DIAL,
	// The button has been pressed for long enough to turn the neopixel strip either on or off
	BTN_TURN_ON_OFF
};

enum VIS_T vis = RAIN;
enum COLORS_T color = R;
enum BUTTON_T button_prev = BTN_OFF, button_curr;
uint32_t button_on_time = 0;

uint32_t sparkles[PIXEL_COUNT];
unsigned long displayTime = 0, randTime, randTimeOut = 1000;
uint32_t idx = 0;
uint8_t fadeSpeed = 3;
uint8_t fadeVal = 250;
bool fadeDir, rainbowDir;
bool inRandShowIncDec = false;
uint8_t numSparkles = 4;
bool turned_on = true;

void RunNeopixels(void *pvParameters)
{
//	Serial.print("RunNeopixels() running on core ");
//	Serial.println(xPortGetCoreID());

	while (true)
	{
		// let the watchdog know that everything is still ok
		TIMERG0.wdt_wprotect=TIMG_WDT_WKEY_VALUE;
		TIMERG0.wdt_feed=1;
		TIMERG0.wdt_wprotect=0;

		// some initial values
		unsigned long diffDisplayTime;
		unsigned long currTime = millis();
		bool changeVis = false;
		int8_t dialVal = 0; // -1 for left, 1 for right, 0 for none

		// get the next rotary encoder value
		dialVal = readDial();
	
		// possibly draw the display
		diffDisplayTime = currTime - displayTime;
		if (diffDisplayTime > 30 || diffDisplayTime < 0)
		{
			if (turned_on) {
				drawVis();
			}
			displayTime = currTime;
			idx++;
		}
		
		// update button values, possibly including dial values for brightness
		if (digitalRead(BUTTON_PIN)) {
			if (button_prev != BTN_OFF) {
				if (turned_on && dialCheckBrightness(dialVal)) {
					dialVal = 0;
					button_curr = BTN_ON_DIAL;
				} else if (button_prev != BTN_ON_DIAL) {
					if (currTime - button_on_time > 5000) {
						turned_on = turnStripOnOff(turned_on);
						while (digitalRead(BUTTON_PIN)) { }
						delay(10); // debounce time
						button_curr = BTN_TURN_ON_OFF;
					}
				}
			}
			if (button_prev != BTN_ON_DIAL && button_curr != BTN_ON_DIAL && button_curr != BTN_TURN_ON_OFF) {
				button_curr = BTN_ON;
			}
			if (button_prev == BTN_OFF) {
				button_on_time = currTime;
				delay(10); // debounce time
			}
		} else {
			switch (button_prev) {
			case BTN_ON:
				changeVis = turned_on;
			case BTN_TURN_ON_OFF:
			case BTN_ON_DIAL:
				delay(10); // debounce time
				break;
			}
			button_curr = BTN_OFF;
		}
		button_prev = button_curr;
	
		// change the visualization
		if (turned_on && changeVis) {
			if (vis == RAINBOW)
				vis = (VIS_T)0;
			else
				vis = (VIS_T)(vis + 1);
			switch (vis) {
			case COLORS:
				Serial.println("COLORS");
				break;
			case COLORS_RAND:
				Serial.println("COLORS_RAND");
				break;
			case FADE:
				Serial.println("FADE");
				break;
			case FADE_RAND:
				Serial.println("FADE_RAND");
				break;
			case RAINBOW:
				Serial.println("RAINBOW");
				break;
			}
			specificVisInit();
		}
	
		// update dial values
		if (turned_on) {
			dialVal = dialCheckSpecificVisIncDec(dialVal);
		}
	
		delay(10);
	}
}

/**
 * Alters the on/off state of the strip, including a cute little visualization to
 * tell you which state you have just entered.
 * 
 * @param turned_on The current state of the strip (true for on, false for off).
 * @return !turned_on
 */
bool turnStripOnOff(bool turned_on) {
	if (turned_on) {
		colorWipe(0, 10);
	} else {
		colorWipe(0x00555555, 10);
	}
	return !turned_on;
}

/**
 * Updates the brightness.
 * If dialval is negative then brightness is decreased.
 * If dialval is positive then brightness is increased.
 * 
 * @return true if the brightness was changed
 */
int dialCheckBrightness(int dialVal)
{
	if (dialVal != 0) {
		int incVal = 5;
		if ((brightness >= 40 && dialVal > 0) || (brightness > 40 && dialVal < 0))
			incVal = 10;
		if ((brightness >= 80 && dialVal > 0) || (brightness > 80 && dialVal < 0))
			incVal = 20;
		Serial.println((dialVal > 0) ? "increasing brightness" : "decreasing brightness");
		if (dialVal < 0) brightness -= incVal;
		if (dialVal > 0) brightness += incVal;
		brightness = min(max(brightness, 5), 250);
		strip.setBrightness(brightness);
		return true;
	}

	return false;
}

int dialCheckSpecificVisIncDec(int dialVal)
{
	if (dialVal != 0)
	{
		if (dialVal < 0) specificVisDecrement();
		if (dialVal > 0) specificVisIncrement();
		dialVal = 0;
	}
	
	return dialVal;
}

void specificVisInit()
{
	int i, delayTime = 100;
	
	switch (vis)
	{
	case FADE:
		fadeVal = 250;
		break;
	case FADE_RAND:
	case COLORS_RAND:
		for (i = 0; i < 4; i++) {
			chooseRandColor();
			colorWipe(colorMap[(int)color], 2);
			show();
			delay(delayTime);
			delayTime *= 1.5;
		}
		randTime = millis();
		break;
	case RAIN:
	case RAINBOW:
	case COLORS:
		// nothing to do
		break;
	}
}

void specificVisDecrement()
{
	switch (vis)
	{
	case COLORS:
	case FADE:
		if ((int)color == 0)
			color = (COLORS_T)(BLACK - 1);
		else
			color = (COLORS_T)(color - 1);
		break;
	case RAIN:
		numSparkles = max(numSparkles - 1, 0);
		break;
	case COLORS_RAND:
	case FADE_RAND:
		randTimeOut -= (randTimeOut <= 200) ? 0 : (log(randTimeOut) / log(2) * 110 - 740);
		randTimeOut = max(randTimeOut, (long unsigned int)200);
		randShowIncDec();
		break;
	case RAINBOW:
		rainbowDir = !rainbowDir;
		break;
	}
}

void specificVisIncrement()
{
	switch (vis)
	{
	case COLORS:
	case FADE:
		if (color == BLACK - 1)
			color = (COLORS_T)0;
		else
			color = (COLORS_T)(color + 1);
		break;
	case RAIN:
		numSparkles = min(numSparkles + 1, PIXEL_COUNT);
		break;
	case COLORS_RAND:
	case FADE_RAND:
		randTimeOut += (randTimeOut >= 60000) ? 0 : (log(randTimeOut) / log(2) * 110 - 740);
		randTimeOut = min(randTimeOut, (long unsigned int)60000);
		randShowIncDec();
		break;
	case RAINBOW:
		rainbowDir = !rainbowDir;
		break;
	}
}

void drawVis()
{
	long diffRandTime = millis() - randTime;
	
	switch (vis)
	{
	case COLORS_RAND:
		if (diffRandTime > randTimeOut || diffRandTime < 0) {
			chooseRandColor();
			randTime = millis();
		}
	case COLORS:
		colorWipe(colorMap[(int)color], 0);
		break;
	case RAIN:
		rain();
		break;
	case FADE_RAND:
		if (fadeVal <= fadeSpeed && fadeDir && (diffRandTime > randTimeOut || diffRandTime < 0)) {
			chooseRandColor();
			randTime = millis();
		}
	case FADE:
		fade();
		break;
	case RAINBOW:
		rainbowCycle();
		break;
	}
	show();
}

void chooseRandColor()
{
	enum COLORS_T currColor = color;
	
	while ((int)currColor == (int)color)
	{
		color = (COLORS_T)random(BLACK-1);
	}
}

// Fill the dots one after the other with a color
void colorWipe(uint32_t c, uint8_t wait) {
	for(uint16_t i=0; i<strip.numPixels(); i++) {
		strip.setPixelColor(i, c);
		if (wait > 0)
			show();
		delay(wait);
	}
}

void fade()
{
	uint32_t rVal = 0, gVal = 0, bVal = 0, colorVal = 0, colorValFade = 0;
	
	// update fade
	if (fadeDir) // increasing
	{
		if (250 - fadeVal < fadeSpeed)
		{
			fadeVal = 250;
			fadeDir = false;
		}
		else
		{
			fadeVal += fadeSpeed;
		}
	}
	else // decreasing
	{
		if (fadeVal < fadeSpeed)
		{
			fadeVal = 0;
			fadeDir = true;
		}
		else
		{
			fadeVal -= fadeSpeed;
		}
	}

	// draw fade
	colorVal = colorMap[(int)color] & 0x00FFFFFF;
	rVal = (colorVal & 0xFF0000) >> 16;
	gVal = (colorVal & 0x00FF00) >> 8;
	bVal = colorVal & 0x0000FF;
	rVal = min(max((double)rVal / 250.0 * (double)fadeVal, 0.0), 250.0);
	gVal = min(max((double)gVal / 250.0 * (double)fadeVal, 0.0), 250.0);
	bVal = min(max((double)bVal / 250.0 * (double)fadeVal, 0.0), 250.0);
	rVal = (rVal << 16) & 0x00FF0000;
	gVal = (gVal <<  8) & 0x0000FF00;
	bVal = bVal & 0x000000FF;
	colorValFade = rVal | gVal | bVal;
	colorWipe(colorValFade, 0);
}

void rain()
{
	sparkle(0x00000032, 0x00969696, 100, numSparkles, 2);
}

/** Creates a faded sparkling animation.
 * @param baseColor The color for non-sparkles.
 * @param sparkleColorLow The brightest possible sparkle color.
 * @param singleChannelRange The smallest span in the R, G, or B channel between baseColor and sparkleColorLow.
 * @param numSparkles The number of pixels to have sparkle at a time.
 * @param subVal The value to subtract from current sparkle pixels.
 */
void sparkle(uint32_t baseColor, uint32_t sparkleColorLow, uint32_t singleChannelRange, uint8_t numSparkles, uint8_t subVal)
{
	uint32_t rangeRnd;
	uint8_t i, pixel, cnt = 0;
	uint8_t *pixPtr, *basePtr;

	// how many sparkles do we have?
	for (pixel = 0; pixel < PIXEL_COUNT; pixel++)
	{
		cnt += (sparkles[pixel] > 0) ? 1 : 0;
	}

	// create new sparkles!
	while (cnt < numSparkles)
	{
		pixel = random(PIXEL_COUNT);
		if (sparkles[pixel] == 0)
		{
			rangeRnd = random(singleChannelRange);
			sparkles[pixel] = ( (sparkleColorLow & 0x00FF0000) + (rangeRnd << 16) |
              						(sparkleColorLow & 0x0000FF00) + (rangeRnd <<  8) |
              						(sparkleColorLow & 0x000000FF) +         rangeRnd );
			cnt++;
		}
	}

	// draw the sparkles
	for (pixel = 0; pixel < PIXEL_COUNT; pixel++)
	{
		if (sparkles[pixel] > 0)
		{
			pixPtr = (uint8_t*)(sparkles + pixel);
			basePtr = (uint8_t*)&baseColor;
			for (i = 0; i < 4; i++)
			{
				if (pixPtr[i] > 0)
				{
					if (pixPtr[i] > subVal)
						pixPtr[i] -= subVal;
					else
						pixPtr[i] = 0;
				}
				if (pixPtr[i] < basePtr[i])
					pixPtr[i] = basePtr[i];
			}
			strip.setPixelColor(pixel, *((uint32_t*)pixPtr) );
			if (sparkles[pixel] == baseColor)
			{
				sparkles[pixel] = 0;
			}
		}
		else
		{
			strip.setPixelColor(pixel, baseColor);
		}
	}
}

void rainbowCycle() {
	int i;

	for (i = 0; i < PIXEL_COUNT; i++)
	{
		strip.setPixelColor(i, Wheel(idx + i * 4));
	}

	show();
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(uint8_t WheelPos) {
	WheelPos = 255 - WheelPos;
	if(WheelPos < 85) {
		return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3,0);
	}
	if(WheelPos < 170) {
		WheelPos -= 85;
		return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3,0);
	}
	WheelPos -= 170;
	return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0,0);
}

void randShowIncDec()
{
	int indicatorLED;
	unsigned long delayTime;
	int dialVal;
	bool timedOut = false;
	volatile int i = 0, j = 1;
	int oldRandShowIncDecIndicatorLED = -1;

	// special check to avoid lots of recursion
	if (inRandShowIncDec)
		return;
	inRandShowIncDec = true;

	// continue until the user stops updating the value
	while (!timedOut)
	{
		// get new variable values
		delayTime = millis();
		indicatorLED = min((int)max((double)randTimeOut / 60000.0 * PIXEL_COUNT, 0.0), PIXEL_COUNT-1);
		Serial.println(randTimeOut);

		// update lighted LED on strip?
		if (oldRandShowIncDecIndicatorLED != indicatorLED)
		{
			colorWipe(0, 0);
			strip.setPixelColor(indicatorLED, 0xFAFAFA);
			strip.setBrightness(250);
			show();
			strip.setBrightness(brightness);
			oldRandShowIncDecIndicatorLED = indicatorLED;
		}

		// delay loop and check for new inc/dec
		timedOut = true;
		while (millis() - delayTime < 500)
		{
			dialVal = readDial();
			if (dialVal != 0)
			{
				dialCheckSpecificVisIncDec(dialVal);
				timedOut = false;
				break;
			}
	
			// Waste time in a pointless loop because this pointless loop does not disable interrupts
			for (i = 0; i < 1000; i++)
				j *= 2;
		}
	}
	
	randTime = millis();
	oldRandShowIncDecIndicatorLED = -1;
	inRandShowIncDec = false;
}

int readDial()
{
	int dialVal = 0;
	
	if (readReady && incDecValid && !hasRead)
	{
		dialVal = incDecValue;
		if (dialVal < 0)
		{
			Serial.println("left");
		}
		else if (dialVal > 0)
		{
			Serial.println("right");
		}
		else
		{
			Serial.println("unknown");
		}
		readReady = false;
		hasRead = true;
	}
	else if (!readReady)// && !incDecValid)
	{
		hasRead = false;
		readReady = true;
	}

	return dialVal;
}






/*-*****************************************************************/
/*-********************** ROTARY ENCODER CODE **********************/
/*-*****************************************************************/






#define BUFF_SIZE 25 // actually one less than this
enum DIAL_T { dNEITHER, dL, dLB, dLR, dR, dRB, dRL, dUNKNOWN }; // rotary encoder dial state type
enum DIAL_T dial_prev = dNEITHER, dial_curr; // rotary encoder dial state
bool lastLHigh = false;
bool lastRHigh = false;
int8_t dialIncDec[BUFF_SIZE];
uint8_t dialReadIdx = 0;
uint8_t dialWriteIdx = 0;

void ReadRotaryEncoder(void *pvParameters)
{
//	Serial.print("ReadRotaryEncoder() running on core ");
//	Serial.println(xPortGetCoreID());

	while (true)
	{
		// let the watchdog know that everything is still ok
		TIMERG0.wdt_wprotect=TIMG_WDT_WKEY_VALUE;
		TIMERG0.wdt_feed=1;
		TIMERG0.wdt_wprotect=0;

		// give FreeRTOS time to do other stuff
		delay(1);
	
		// update dial values with exact result
		bool newLHigh = digitalRead(DIAL_L_PIN);
		bool newRHigh = digitalRead(DIAL_R_PIN);
		dialCheck(lastLHigh, lastRHigh);
		lastLHigh = newLHigh;
		lastRHigh = newRHigh;
//		Serial.print(newLHigh);
//		Serial.print(":");
//		Serial.println(newRHigh);

		// check if the lighting controller code
		// running on core 1 in loop() is ready for the next value
		uint8_t availReadCnt = getAvailReadCnt();
		if (availReadCnt > 0 && readReady && !incDecValid)
		{
			incDecValue = dialIncDec[dialReadIdx];
			incDecValid = true;
			availReadCnt--;
			dialReadIdx = (dialReadIdx + 1) % BUFF_SIZE;
		}

		// check if the signal has been read yet
		else if (hasRead && incDecValid)
		{
			incDecValue = 0;
			incDecValid = false;
		}
	}
}

// returns the number of values available in the buffer space of dialIncDec[]
uint8_t getAvailReadCnt()
{
	if (dialWriteIdx >= dialReadIdx)
		return dialWriteIdx - dialReadIdx;
	else
		return dialWriteIdx + BUFF_SIZE - dialReadIdx;
}

// read the rotary encoder dial value and add that value to dialIncDec
void dialCheck(bool lHigh, bool rHigh)
{
	// check if the buffer has space available
	uint8_t availReadCnt = getAvailReadCnt();
	if (availReadCnt + 1 >= BUFF_SIZE)
		return;
	
	// all the condition checking ever
	if (lHigh)
	{
		if (rHigh)
		{ // L and R
			switch (dial_prev) {
			case dLB:
			case dRB:
			case dLR:
			case dRL:
				dial_curr = dial_prev;
				break;
			case dL:
//				Serial.println("dLB");
				dial_curr = dLB;
				break;
			case dR:
//				Serial.println("dRB");
				dial_curr = dRB;
				break;
			default:
//				Serial.println("dUNKNOWN");
				dial_curr = dUNKNOWN;
			}
		}
		else
		{ // L, no R
			switch (dial_prev) {
			case dL:
			case dLB:
			case dRL:
				dial_curr = dial_prev;
				break;
			case dRB:
//				Serial.println("dRL");
				dial_curr = dRL;
				break;
			case dNEITHER:
//				Serial.println("dL");
				dial_curr = dL;
				break;
			default:
//				Serial.println("dUNKNOWN");
				dial_curr = dUNKNOWN;
			}
		}
	}
	else
	{
		if (rHigh)
		{ // R, no L
			switch (dial_prev) {
			case dR:
			case dRB:
			case dLR:
				dial_curr = dial_prev;
				break;
			case dLB:
//				Serial.println("dLR");
				dial_curr = dLR;
				break;
			case dNEITHER:
//				Serial.println("dR");
				dial_curr = dR;
				break;
			default:
//				Serial.println("dUNKNOWN");
				dial_curr = dUNKNOWN;
			}
		}
		else
		{ // no L, no R
			switch (dial_prev) {
			case dNEITHER:
				dial_curr = dial_prev;
				break;
			case dRL:
//				Serial.println("decrement");
				dialIncDec[dialWriteIdx] = -1;
				dialWriteIdx = (dialWriteIdx + 1) % BUFF_SIZE;
				dial_curr = dNEITHER;
				break;
			case dLR:
//				Serial.println("increment");
				dialIncDec[dialWriteIdx] = 1;
				dialWriteIdx = (dialWriteIdx + 1) % BUFF_SIZE;
				dial_curr = dNEITHER;
				break;
			default:
//				Serial.println("dNEITHER");
				dial_curr = dNEITHER;
			}
		}
	}
	dial_prev = dial_curr;
}
