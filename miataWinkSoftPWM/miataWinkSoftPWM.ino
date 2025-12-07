// Miata headlight motor controller
// ESP32 DevKit V1
// Non-blocking + soft-start PWM via MOSFETs

#define buttonPin 33
#define leftupPin 25
#define rightupPin 26
#define leftdownPin 27
#define rightdownPin 14

// ==========================================
// PWM CONFIG
#define PWM_FREQ       1500      // Hz (safe for motors, quiet)
#define PWM_RESOLUTION 8         // 8-bit (0–255)
#define PWM_MAX        255
#define PWM_RAMP_TIME  150       // ms soft-start ramp
#define MOTOR_TIME     750
#define MAX_MOTOR_TIME 900

// PWM channels
#define CH_LEFT_UP     0
#define CH_RIGHT_UP    1
#define CH_LEFT_DOWN   2
#define CH_RIGHT_DOWN  3

// Bitmask helpers
#define MASK(ch) (1 << (ch))

// ==========================================
bool ledVal = false;

enum MotionState { IDLE, STEP1, STEP2 };
enum MotionType  { NONE, WINK, BOTH_TOGGLE, SPLIT, RESET };

MotionState motionState = IDLE;
MotionType motionType = NONE;

unsigned long motionStart = 0;
unsigned long pwmRampStart = 0;

// Active PWM channels
uint8_t activeMask = 0;

// ==========================================
// Button timing
int debounce = 50;
int DCgap = 250;
int holdTime = 500;
int longHoldTime = 1500;

// Button state
bool buttonVal = HIGH;
bool buttonLast = HIGH;
bool DCwaiting = false;
bool DConUp = false;
bool singleOK = true;

unsigned long downTime = 0;
unsigned long upTime = 0;

bool ignoreUp = false;
bool waitForUp = false;
bool holdEventPast = false;
bool longHoldEventPast = false;

// ==========================================
// Utility: PWM helpers

void pwmOffAll() {
  ledcWrite(CH_LEFT_UP, 0);
  ledcWrite(CH_RIGHT_UP, 0);
  ledcWrite(CH_LEFT_DOWN, 0);
  ledcWrite(CH_RIGHT_DOWN, 0);
  activeMask = 0;
}

void pwmBegin(unsigned ch) {
  ledcWrite(ch, 0);   // barely on
  activeMask |= MASK(ch);
  pwmRampStart = millis();
}

void pwmUpdate(unsigned ch) {
  if (!(activeMask & MASK(ch))) return;

  unsigned long elapsed = millis() - pwmRampStart;
  if (elapsed >= PWM_RAMP_TIME) {
    ledcWrite(ch, PWM_MAX);
  } else {
    uint8_t duty = map(elapsed, 0, PWM_RAMP_TIME, 0, PWM_MAX);
    ledcWrite(ch, duty);
  }
}

void pwmUpdateActive() {
  for (uint8_t ch = 0; ch < 4; ch++) {
    pwmUpdate(ch);
  }
}

// ==========================================
void setup() {
  pinMode(buttonPin, INPUT_PULLUP);

  ledcSetup(CH_LEFT_UP,   PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(CH_RIGHT_UP,  PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(CH_LEFT_DOWN, PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(CH_RIGHT_DOWN,PWM_FREQ, PWM_RESOLUTION);

  ledcAttachPin(leftupPin,   CH_LEFT_UP);
  ledcAttachPin(rightupPin,  CH_RIGHT_UP);
  ledcAttachPin(leftdownPin, CH_LEFT_DOWN);
  ledcAttachPin(rightdownPin,CH_RIGHT_DOWN);

  pwmOffAll();
}

// ==========================================
void loop() {
  int b = checkButton();

  if (b == 4) {
    startResetEvent();
  }
  else if (motionState == IDLE) {
    if (b == 1) startWinkEvent();
    if (b == 2) startBothToggleEvent();
    if (b == 3) startSplitEvent();
  }

  if (motionState != IDLE) {
    updateMotion();
  }
}

// ==========================================
// Motion starters

void startWinkEvent() {
  motionType = WINK;
  motionState = STEP1;
  motionStart = millis();
  pwmRampStart = millis();

  if (!ledVal) pwmBegin(CH_LEFT_UP);
  else         pwmBegin(CH_LEFT_DOWN);
}

void startBothToggleEvent() {
  ledVal = !ledVal;

  motionType = BOTH_TOGGLE;
  motionState = STEP1;
  motionStart = millis();
  pwmRampStart = millis();

  if (ledVal) {
    pwmBegin(CH_LEFT_UP);
    pwmBegin(CH_RIGHT_UP);
  } else {
    pwmBegin(CH_LEFT_DOWN);
    pwmBegin(CH_RIGHT_DOWN);
  }
}

void startSplitEvent() {
  ledVal = !ledVal;

  motionType = SPLIT;
  motionState = STEP1;
  motionStart = millis();
  pwmRampStart = millis();

  pwmBegin(CH_LEFT_UP);
  pwmBegin(CH_RIGHT_DOWN);
}

void startResetEvent() {
  motionType = RESET;
  motionState = STEP1;
  motionStart = millis();
  pwmRampStart = millis();

  ledVal = false;

  pwmBegin(CH_LEFT_DOWN);
  pwmBegin(CH_RIGHT_DOWN);
}

// ==========================================
// Motion update

void updateMotion() {
  unsigned long now = millis();

  if (now - motionStart > MAX_MOTOR_TIME) {
    pwmOffAll();
    motionState = IDLE;
    motionType = NONE;
    return;
  }

  pwmUpdateActive();

  if (motionState == STEP1 && now - motionStart >= MOTOR_TIME) {
    pwmOffAll();
    motionState = (motionType == WINK) ? STEP2 : IDLE;
    motionStart = now;

    if (motionState == STEP2) {
      pwmRampStart = millis();
      if (!ledVal) pwmBegin(CH_LEFT_DOWN);
      else         pwmBegin(CH_LEFT_UP);
    } else {
      motionType = NONE;
    }
  }
  else if (motionState == STEP2 && now - motionStart >= MOTOR_TIME) {
    pwmOffAll();
    motionState = IDLE;
    motionType = NONE;
  }
}

// ==========================================
// Button logic (unchanged)

int checkButton() {
  int event = 0;
  buttonVal = digitalRead(buttonPin);

  if (buttonVal == LOW && buttonLast == HIGH && (millis() - upTime) > debounce) {
    downTime = millis();
    ignoreUp = false;
    waitForUp = false;
    singleOK = true;
    holdEventPast = false;
    longHoldEventPast = false;

    if ((millis() - upTime) < DCgap && !DConUp && DCwaiting)
      DConUp = true;
    else
      DConUp = false;

    DCwaiting = false;
  }
  else if (buttonVal == HIGH && buttonLast == LOW && (millis() - downTime) > debounce) {
    if (!ignoreUp) {
      upTime = millis();
      if (!DConUp) {
        DCwaiting = true;
      } else {
        event = 2;
        DConUp = false;
        DCwaiting = false;
        singleOK = false;
      }
    }
  }

  if (buttonVal == HIGH && DCwaiting && !DConUp && singleOK && (millis() - upTime) >= DCgap) {
    event = 1;
    DCwaiting = false;
  }

  if (buttonVal == LOW && (millis() - downTime) >= holdTime) {
    if (!holdEventPast) {
      event = 3;
      ignoreUp = true;
      DCwaiting = false;
      downTime = millis();
      holdEventPast = true;
    }

    if ((millis() - downTime) >= longHoldTime && !longHoldEventPast) {
      event = 4;
      longHoldEventPast = true;
    }
  }

  buttonLast = buttonVal;
  return event;
}
