// miata headlight motor functions (ESP32, non-blocking, sleeper build)

#define buttonPin 33     // factory headlight switch input
#define leftupPin 25        // left headlight up
#define rightupPin 26       // right headlight up
#define leftdownPin 27      // left headlight down
#define rightdownPin 14     // right headlight down

// ==========================================
// State tracking
bool ledVal = false;    // logical "headlights up" state

enum MotionState {
  IDLE,
  STEP1,
  STEP2
};

enum MotionType {
  NONE,
  WINK,
  BOTH_TOGGLE,
  SPLIT,
  RESET
};

MotionState motionState = IDLE;
MotionType motionType = NONE;

unsigned long motionStart = 0;

// Motor safety (factory-ish cutoff)
#define MOTOR_TIME 750
#define MAX_MOTOR_TIME 900

// ==========================================
// Button timing
int debounce = 50;
int DCgap = 250;            // max ms between clicks for a double click event
int holdTime = 500;         // ms hold period: how long to wait for press+hold event
int longHoldTime = 1500;    // ms long hold period: how long to wait for press+hold event

// Button state vars
bool buttonVal = HIGH;
bool buttonLast = HIGH;
bool DCwaiting = false;     // whether we're waiting for a double click (down)
bool DConUp = false;        // whether to register a double click on next release, or whether to wait and click
bool singleOK = true;       // whether it's OK to do a single click

unsigned long downTime = 0;
unsigned long upTime = 0;

bool ignoreUp = false;          // whether to ignore the button release because the click+hold was triggered
bool waitForUp = false;         // when held, whether to wait for the up event
bool holdEventPast = false;     // whether or not the hold event happened already
bool longHoldEventPast = false; // whether or not the long hold event happened already

// ==========================================
// Utility
void allOff() {
  digitalWrite(leftupPin, LOW);
  digitalWrite(rightupPin, LOW);
  digitalWrite(rightupPin, LOW);
  digitalWrite(rightdownPin, LOW);
}

// ==========================================
void setup() {
  pinMode(buttonPin, INPUT_PULLUP);

  pinMode(leftupPin, OUTPUT);
  pinMode(rightupPin, OUTPUT);
  pinMode(rightupPin, OUTPUT);
  pinMode(rightdownPin, OUTPUT);

  allOff();
}

// ==========================================
void loop() {
  int b = checkButton();

  // Panic reset always wins
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

  digitalWrite(leftupPin, !ledVal);
  digitalWrite(rightupPin, ledVal);
}

void startBothToggleEvent() {
  ledVal = !ledVal;

  motionType = BOTH_TOGGLE;
  motionState = STEP1;
  motionStart = millis();

  digitalWrite(leftupPin, ledVal);
  digitalWrite(rightupPin, ledVal);
  digitalWrite(rightupPin, !ledVal);
  digitalWrite(rightdownPin, !ledVal);
}

void startSplitEvent() {
  ledVal = !ledVal;

  motionType = SPLIT;
  motionState = STEP1;
  motionStart = millis();

  digitalWrite(leftupPin, !ledVal);
  digitalWrite(rightupPin, ledVal);
  digitalWrite(rightupPin, ledVal);
  digitalWrite(rightdownPin, !ledVal);
}

void startResetEvent() {
  motionType = RESET;
  motionState = STEP1;
  motionStart = millis();

  ledVal = false;

  digitalWrite(leftupPin, LOW);
  digitalWrite(rightupPin, LOW);
  digitalWrite(rightupPin, HIGH);
  digitalWrite(rightdownPin, HIGH);
}

// ==========================================
// Non-blocking motion handler

void updateMotion() {
  unsigned long now = millis();

  // Absolute motor hard cutoff (safety)
  if (now - motionStart > MAX_MOTOR_TIME) {
    allOff();
    motionState = IDLE;
    motionType = NONE;
    return;
  }

  if (motionState == STEP1 && now - motionStart >= MOTOR_TIME) {
    if (motionType == WINK) {
      motionState = STEP2;
      motionStart = now;

      digitalWrite(leftupPin, ledVal);
      digitalWrite(rightupPin, !ledVal);
    } else {
      allOff();
      motionState = IDLE;
      motionType = NONE;
    }
  }
  else if (motionState == STEP2 && now - motionStart >= MOTOR_TIME) {
    allOff();
    motionState = IDLE;
    motionType = NONE;
  }
}

// ==========================================
// Button logic (unchanged behavior)

int checkButton() {
  int event = 0;
  buttonVal = digitalRead(buttonPin);

  // Button down
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

  // Button up
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

  // Single click
  if (buttonVal == HIGH && DCwaiting && !DConUp && singleOK && (millis() - upTime) >= DCgap) {
    event = 1;
    DCwaiting = false;
  }

  // Hold
  if (buttonVal == LOW && (millis() - downTime) >= holdTime) {
    if (!holdEventPast) {
      event = 3;
      waitForUp = true;
      ignoreUp = true;
      DConUp = false;
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
