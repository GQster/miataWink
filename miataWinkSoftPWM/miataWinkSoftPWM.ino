// Miata headlight motor controller
// ESP32 DevKit V1
// Non-blocking + soft-start PWM via MOSFETs

#include <Arduino.h>

// ===================== PINS =====================
#define buttonPin 33

#define leftupPin 25
#define leftdownPin 26

#define rightupPin 27
#define rightdownPin 14

// ===================== PWM =====================
#define PWM_FREQ       1500      // Hz (safe for motors, quiet)
#define PWM_RESOLUTION 8         // 8-bit (0–255)
#define PWM_MAX        255
#define PWM_RAMP_TIME  150       // ms soft-start ramp
#define MOTOR_TIME     750      // ms motor activation time for up/down         // TODO: adjust timing as needed
#define MAX_MOTOR_TIME 900

#define CH_LEFT_UP     0
#define CH_RIGHT_UP    1
#define CH_LEFT_DOWN   2
#define CH_RIGHT_DOWN  3

// Bitmask helpers
#define MASK(ch) (1 << (ch))


// ===================== BUTTON =====================
#define DEBOUNCE_MS    30
#define CLICK_TIMEOUT  500
#define LONG_HOLD_TIME 2000

// ===================== STATE =====================
bool ledVal = false; // false = down, true = up

enum MotionState {
  IDLE, STEP1, STEP2,
  WAVE1, WAVE2, WAVE3, WAVE4
};

enum MotionType {
  NONE, BOTH_TOGGLE, WINK, WAVE, SPLIT, RESET
};

MotionState motionState = IDLE;
MotionType motionType = NONE;

unsigned long motionStart = 0;
unsigned long pwmRampStart = 0;
uint8_t activeMask = 0;

// ===================== BUTTON STATE =====================
bool rawState = HIGH;
bool lastRawState = HIGH;
bool debouncedState = HIGH;
bool lastDebouncedState = HIGH;

unsigned long lastBounceTime = 0;
unsigned long downTime = 0;
unsigned long lastReleaseTime = 0;

int clickCount = 0;
bool waitingForMoreClicks = false;
bool longHoldFired = false;

// ===================== PWM HELPERS =====================
const int motorPins[] = {
  leftupPin, rightupPin, leftdownPin, rightdownPin
};

const int motorChannels[] = {
  CH_LEFT_UP, CH_RIGHT_UP, CH_LEFT_DOWN, CH_RIGHT_DOWN
};

void pwmOffAll() {
  for (int i = 0; i < 4; i++) ledcWrite(motorPins[i], 0);
  activeMask = 0;
}

void pwmBegin(int pin) {
  // start ramp for the specified pin
  ledcWrite(pin, 0);   // ensure starts low
  int ch = 0;
  if (pin == leftupPin) ch = CH_LEFT_UP;
  else if (pin == rightupPin) ch = CH_RIGHT_UP;
  else if (pin == leftdownPin) ch = CH_LEFT_DOWN;
  else if (pin == rightdownPin) ch = CH_RIGHT_DOWN;
  activeMask |= MASK(ch);
  pwmRampStart = millis();
}

void pwmUpdate() {
  unsigned long dt = millis() - pwmRampStart;
  uint8_t duty = (dt >= PWM_RAMP_TIME)
                   ? PWM_MAX
                   : map(dt, 0, PWM_RAMP_TIME, 0, PWM_MAX);

  for (int i = 0; i < 4; i++) {
    if (activeMask & MASK(motorChannels[i])) {
      ledcWrite(motorPins[i], duty);
    }
  }
}

// ===================== SETUP =====================
void setup() {
  pinMode(buttonPin, INPUT_PULLUP);

  ledcAttachChannel(leftupPin,   PWM_FREQ, PWM_RESOLUTION, CH_LEFT_UP);
  ledcAttachChannel(rightupPin,  PWM_FREQ, PWM_RESOLUTION, CH_RIGHT_UP);
  ledcAttachChannel(leftdownPin, PWM_FREQ, PWM_RESOLUTION, CH_LEFT_DOWN);
  ledcAttachChannel(rightdownPin,PWM_FREQ, PWM_RESOLUTION, CH_RIGHT_DOWN);

  pwmOffAll();
  // Optional: enable Serial for debugging
  // Serial.begin(115200);

  // Reset on startup
  startReset();
}

// ===================== LOOP =====================
void loop() {
  int evt = checkButton();

  // long-hold (reset) handled as immediate event code 5
  if (evt == 5) startReset();
  else if (motionState == IDLE) {
    if (evt == 1) startBothToggle();   // single click -> OEM toggle both
    if (evt == 2) startWink();         // double -> wink
    if (evt == 3) startWave();         // triple -> wave
    if (evt == 4) startSplit();        // quadruple -> split
  }

  if (motionState != IDLE) updateMotion();
}

// ===================== MOTIONS =====================
void startBothToggle() {
  ledVal = !ledVal;
  motionType = BOTH_TOGGLE;
  motionState = STEP1;
  motionStart = millis();
  if (ledVal) {
    pwmBegin(leftupPin); 
    pwmBegin(rightupPin);
  } else {
    pwmBegin(leftdownPin);
    pwmBegin(rightdownPin);
  }
}

void startWink() {
  motionType = WINK;
  motionState = STEP1;
  motionStart = millis();
  if (!ledVal) pwmBegin(leftupPin);
  else pwmBegin(leftdownPin);
}

void startSplit() {
  motionType = SPLIT;
  motionState = STEP1;
  motionStart = millis();

  if (!ledVal) {
    pwmBegin(leftupPin);
    pwmBegin(rightdownPin);
  } else {
    pwmBegin(leftdownPin);
    pwmBegin(rightupPin);
  }
  ledVal = !ledVal;
}

void startWave() {
  motionType = WAVE;
  motionState = WAVE1;
  motionStart = millis();
  pwmOffAll();
}

void startReset() {
  motionType = RESET;
  motionState = STEP1;
  motionStart = millis();
  ledVal = false;
  pwmBegin(leftdownPin);
  pwmBegin(rightdownPin);
}

// ===================== MOTION UPDATE =====================
void updateMotion() {
  unsigned long now = millis();
  pwmUpdate();

  if (now - motionStart > MAX_MOTOR_TIME) {
    pwmOffAll();
    motionState = IDLE;
    motionType = NONE;
    return;
  }

  switch (motionType) {
    case WINK:
      if (motionState == STEP1 && now - motionStart >= MOTOR_TIME) {
        pwmOffAll();
        motionState = STEP2;
        motionStart = now;
        if (!ledVal) pwmBegin(leftdownPin);
        else pwmBegin(leftupPin);
      } else if (motionState == STEP2 && now - motionStart >= MOTOR_TIME) {
        pwmOffAll();
        motionState = IDLE;
        motionType = NONE;
      }
      break;

    case BOTH_TOGGLE:
    case SPLIT:
    case RESET:
      if (now - motionStart >= MOTOR_TIME) {
        pwmOffAll();
        motionState = IDLE;
        motionType = NONE;
      }
      break;

    case WAVE:
      if (motionState == WAVE1 && now - motionStart > MOTOR_TIME/2) {
        pwmBegin(leftupPin); 
        motionState = WAVE2;
      } else if (motionState == WAVE2 && now - motionStart > MOTOR_TIME) {
        pwmBegin(rightupPin); 
        motionState = WAVE3;
      } else if (motionState == WAVE3 && now - motionStart > MOTOR_TIME*1.5) {
        pwmBegin(leftdownPin); 
        motionState = WAVE4;
      } else if (motionState == WAVE4 && now - motionStart > MOTOR_TIME*2) {
        pwmBegin(rightdownPin);
        motionState = IDLE;
        motionType = NONE;
      }
      break;

    default:
      break;
  }
}

// ===================== BUTTON HANDLER =====================
// Returns:
// 0 = nothing
// 1 = single click (toggle both)
// 2 = double click (wink)
// 3 = triple click (wave)
// 4 = quadruple click (split)
// 5 = long hold (reset)

int checkButton() {
  rawState = digitalRead(buttonPin);

  if (rawState != lastRawState) lastBounceTime = millis();

  if ((millis() - lastBounceTime) > DEBOUNCE_MS) {
    if (rawState != debouncedState) {
      debouncedState = rawState;

      // PRESS
      if (debouncedState == LOW) {
    downTime = millis();
        longHoldFired = false;
      }

      // RELEASE
      if (debouncedState == HIGH) {
        if (!longHoldFired) {
      clickCount++;
          lastReleaseTime = millis();
          waitingForMoreClicks = true;
        }
      }
    }
  }

  // LONG HOLD
  if (debouncedState == LOW && !longHoldFired) {
    if (millis() - downTime >= LONG_HOLD_TIME) {
      longHoldFired = true;
      clickCount = 0;
      waitingForMoreClicks = false;
      lastRawState = rawState;
      return 5; // RESET
    }
  }

  // multi-click timeout: decide which click event it was
  if (waitingForMoreClicks && (millis() - lastReleaseTime) > CLICK_TIMEOUT) {
    // clamp clicks to 4 for our mapping
    int c = clickCount;
    clickCount = 0;
    waitingForMoreClicks = false;
    lastRawState = rawState;
    if (c == 1) return 1;
    if (c == 2) return 2;
    if (c == 3) return 3;
    if (c >= 4) return 4;
  }

  lastRawState = rawState;
  return 0;
}
