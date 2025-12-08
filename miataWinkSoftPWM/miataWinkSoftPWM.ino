// Miata headlight motor controller
// ESP32 DevKit V1
// Non-blocking + soft-start PWM via MOSFETs

#include <Arduino.h>

// ===================== PINS =====================
#define buttonPin     33

#define leftupPin     25
#define leftdownPin   26
#define rightupPin    27
#define rightdownPin  14

// ===================== PWM =====================
#define PWM_FREQ       1500     // Hz (safe for motors, quiet)
#define PWM_RESOLUTION 8        // 8-bit (0–255)
#define PWM_MAX        255
#define PWM_RAMP_TIME  150      // ms soft-start ramp
#define MOTOR_TIME     750      // ms motor activation time for up/down         // TODO: adjust timing as needed
#define MAX_MOTOR_TIME 900    // ms maximum motor time as safety cutoff

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
enum HeadlightPos { DOWN, UP };
HeadlightPos headlightPos = DOWN;

bool splitFlipped = false;

enum MotionState {
  IDLE,
  ACTIVE,
  WINKING,
  WAVING
};

MotionState motionState = IDLE;

unsigned long motionStart = 0;
unsigned long pwmRampStart = 0;
uint8_t activeMask = 0;

// ===================== WAVE =====================
const int waveSequence[] = {
  leftupPin, rightupPin,
  leftdownPin, rightdownPin,
  leftupPin, rightupPin,
  leftdownPin, rightdownPin
};
int winkStep = 0; 
const int WAVE_STEPS = sizeof(waveSequence) / sizeof(waveSequence[0]);
int waveStep = 0;
unsigned long waveStepStart = 0;

// ===================== BUTTON STATE =====================
bool stableState = HIGH;
bool lastReading = HIGH;
unsigned long lastDebounceTime = 0;

unsigned long pressTime = 0;
unsigned long lastReleaseTime = 0;

int clickCount = 0;
bool waitingForClicks = false;
bool longHoldFired = false;

// ===================== PWM HELPERS =====================
void pwmOffAll() {
  ledcWrite(leftupPin, 0);
  ledcWrite(rightupPin, 0);
  ledcWrite(leftdownPin, 0);
  ledcWrite(rightdownPin, 0);
  activeMask = 0;
}

void pwmBegin(int pin) {
  int ch =
    (pin == leftupPin)    ? CH_LEFT_UP :
    (pin == rightupPin)   ? CH_RIGHT_UP :
    (pin == leftdownPin)  ? CH_LEFT_DOWN :
                            CH_RIGHT_DOWN;

  activeMask |= MASK(ch);
  pwmRampStart = millis();
}

void pwmUpdate() {
  unsigned long dt = millis() - pwmRampStart;
  uint8_t duty = (dt >= PWM_RAMP_TIME)
    ? PWM_MAX
    : map(dt, 0, PWM_RAMP_TIME, 0, PWM_MAX);

  if (activeMask & MASK(CH_LEFT_UP))    ledcWrite(leftupPin, duty);
  if (activeMask & MASK(CH_RIGHT_UP))   ledcWrite(rightupPin, duty);
  if (activeMask & MASK(CH_LEFT_DOWN))  ledcWrite(leftdownPin, duty);
  if (activeMask & MASK(CH_RIGHT_DOWN)) ledcWrite(rightdownPin, duty);
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

  if (evt == 5) {
    startReset();
    return;
  }

  if (motionState != IDLE) {
    updateMotion();
    return;
  }

  if      (evt == 1) startBothToggle();
  else if (evt == 2) startWink();
  else if (evt == 3) startWave();
  else if (evt == 4) startSplit();
}

// ===================== MOTIONS =====================
void startBothToggle() {
  motionState = ACTIVE;
  motionStart = millis();

  if (headlightPos == DOWN) {
    pwmBegin(leftupPin);
    pwmBegin(rightupPin);
    headlightPos = UP;
  } else {
    pwmBegin(leftdownPin);
    pwmBegin(rightdownPin);
    headlightPos = DOWN;
  }
}

void startWink() {
  motionState = WINKING;
  motionStart = millis();
  winkStep = 1;
  pwmBegin((headlightPos == UP) ? leftdownPin : leftupPin);
}

void startSplit() {
  motionState = ACTIVE;
  motionStart = millis();
  splitFlipped = !splitFlipped;

  if (!splitFlipped) {
    pwmBegin(leftupPin);
    pwmBegin(rightdownPin);
  } else {
    pwmBegin(leftdownPin);
    pwmBegin(rightupPin);
  }
}

void startReset() {
  pwmOffAll();
  motionState = ACTIVE;
  motionStart = millis();
  headlightPos = DOWN;
  splitFlipped = false;

  pwmBegin(leftdownPin);
  pwmBegin(rightdownPin);
}

void startWave() {
  // Initialize wave by turning both headlights down
  pwmOffAll();
  pwmBegin(leftdownPin);
  pwmBegin(rightdownPin);

  motionState = WAVING;
  waveStep = 0;
  waveStepStart = millis();
  pwmOffAll();
  pwmBegin(waveSequence[0]);
}

// ===================== MOTION UPDATE =====================
void updateMotion() {
  pwmUpdate();
  unsigned long now = millis();

  if (motionState == WINKING){
    if (now - motionStart >= MOTOR_TIME) {
      pwmOffAll();
      if (winkStep >= 2) {
        motionState = IDLE;
        return;
      }
      pwmBegin((headlightPos == UP) ? leftupPin : leftdownPin);
      motionStart = now;
      winkStep++;
    }
  }
  else if (motionState == WAVING) {
    if (now - waveStepStart >= MOTOR_TIME) {
      pwmOffAll();
      waveStep++;
      if (waveStep >= WAVE_STEPS) {
        motionState = IDLE;
        return;
      }
      waveStepStart = now;
      pwmBegin(waveSequence[waveStep]);
    }
    return;
  }

  if (now - motionStart >= MAX_MOTOR_TIME) {
    pwmOffAll();
    motionState = IDLE;
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
  bool reading = digitalRead(buttonPin);

  if (reading != lastReading)
    lastDebounceTime = millis();

  if (millis() - lastDebounceTime > DEBOUNCE_MS) {
    if (reading != stableState) {
      stableState = reading;

      if (stableState == LOW) {
        pressTime = millis();
        longHoldFired = false;
      } else if (!longHoldFired) {
          clickCount++;
          lastReleaseTime = millis();
          waitingForClicks = true;
      }
    }
  }

  lastReading = reading;

  if (stableState == LOW && !longHoldFired &&
      millis() - pressTime >= LONG_HOLD_TIME) {
    longHoldFired = true;
    clickCount = 0;
    waitingForClicks = false;
    return 5;
  }

  // multi-click timeout: decide which click event it was
  if (waitingForClicks && millis() - lastReleaseTime >= CLICK_TIMEOUT) {
    // clamp clicks to 4 for our mapping
    int c = clickCount;
    clickCount = 0;
    waitingForClicks = false;
    return (c >= 4) ? 4 : c;
  }

  return 0;
}
