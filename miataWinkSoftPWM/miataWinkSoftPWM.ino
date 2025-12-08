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
#define MOTOR_TIME     750      // ms motor activation time for up/down
#define MAX_MOTOR_TIME 900      // ms maximum motor time as safety cutoff
#define WAVE_STEP_DELAY  (MOTOR_TIME*0.75) // Time between wave steps (overlap timing)

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
// track each headlight physical position independently
// headUp[0] = left (true = up), headUp[1] = right
bool headUp[2] = { false, false };

bool splitFlipped = false;

enum MotionState {
  IDLE,
  ACTIVE,
  WINKING,
  WAVING,
  QUEUED_WAVE 
  };

MotionState motionState = IDLE;

unsigned long pwmRampStartCh[4]; // one for each channel
unsigned long motorStopCh[4];
uint8_t activeMask = 0;
unsigned long motionStart = 0;

// ===================== WINK =====================
int winkStep = 0; // 0 = none, 1 = first movement, 2 = return
bool winkLeftNext = true;  // alternates which head winks
uint8_t winkSide = 0;     // 0 = left, 1 = right (active wink side)
bool winkOriginalUp = false;

// ===================== WAVE =====================
// sequence: left up, right up, left down, right down, repeated once
const int waveSequence[] = {
  leftupPin, rightupPin,
  leftdownPin, rightdownPin,
  leftupPin, rightupPin,
  leftdownPin, rightdownPin
};
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
int pinToChannel(int pin) {
  if (pin == leftupPin) return CH_LEFT_UP;
  if (pin == rightupPin) return CH_RIGHT_UP;
  if (pin == leftdownPin) return CH_LEFT_DOWN;
  return CH_RIGHT_DOWN;
}

int channelToPin(int ch) {
  switch (ch) {
    case CH_LEFT_UP: return leftupPin;
    case CH_RIGHT_UP: return rightupPin;
    case CH_LEFT_DOWN: return leftdownPin;
    case CH_RIGHT_DOWN: return rightdownPin;
    default: return -1;
}
}

void setHeadStateFromChannelFinish(int ch) {
  // update headUp[] when a channel finishes
  switch (ch) {
    case CH_LEFT_UP:  headUp[0] = true;  break;
    case CH_LEFT_DOWN: headUp[0] = false; break;
    case CH_RIGHT_UP: headUp[1] = true;  break;
    case CH_RIGHT_DOWN: headUp[1] = false; break;
  }
}

// ===================== PWM (per-channel) =====================
void pwmOffAll() {
  ledcWrite(leftupPin, 0);
  ledcWrite(rightupPin, 0);
  ledcWrite(leftdownPin, 0);
  ledcWrite(rightdownPin, 0);
  activeMask = 0;
  // clear scheduled stops (not required but safer)
  for (int i=0;i<4;i++) motorStopCh[i]=0;
  for (int i=0;i<4;i++) pwmRampStartCh[i]=0;
}

void pwmBegin(int pin) {
  int ch = pinToChannel(pin);
  if (ch < 0) return;
  // If already active, restart the scheduled stop to extend duration
  // (we keep existing ramp start if already active so it doesn't retrigger ramp abruptly)
  if (!(activeMask & MASK(ch))) {
    pwmRampStartCh[ch] = millis();
  }
  motorStopCh[ch] = millis() + MOTOR_TIME;
  activeMask |= MASK(ch);
  // we don't write output immediately; pwmUpdate will ramp
}

void pwmUpdate() {
  unsigned long now = millis();
  for (int ch = 0; ch < 4; ch++) {
    if (!(activeMask & MASK(ch))) continue;

    // finished?
    if (now >= motorStopCh[ch]) {
      // stop channel and update head state
      activeMask &= ~MASK(ch);
      int pin = channelToPin(ch);
      if (pin >= 0) ledcWrite(pin, 0);
      setHeadStateFromChannelFinish(ch);
      continue;
    }

    // ramp duty
    unsigned long dt = now - pwmRampStartCh[ch];
    uint8_t duty = (dt >= PWM_RAMP_TIME) ? PWM_MAX : map(dt, 0, PWM_RAMP_TIME, 0, PWM_MAX);
    switch (ch) {
      case CH_LEFT_UP:    ledcWrite(leftupPin, duty); break;
      case CH_RIGHT_UP:   ledcWrite(rightupPin, duty); break;
      case CH_LEFT_DOWN:  ledcWrite(leftdownPin, duty); break;
      case CH_RIGHT_DOWN: ledcWrite(rightdownPin, duty); break;
    }
    }
  }

// ===================== SETUP =====================
void setup() {
  pinMode(buttonPin, INPUT_PULLUP);

  // attach channels (new core API expects pin, freq, res, channel)
  ledcAttachChannel(leftupPin,   PWM_FREQ, PWM_RESOLUTION, CH_LEFT_UP);
  ledcAttachChannel(rightupPin,  PWM_FREQ, PWM_RESOLUTION, CH_RIGHT_UP);
  ledcAttachChannel(leftdownPin, PWM_FREQ, PWM_RESOLUTION, CH_LEFT_DOWN);
  ledcAttachChannel(rightdownPin,PWM_FREQ, PWM_RESOLUTION, CH_RIGHT_DOWN);

  pwmOffAll();
  // Serial.begin(115200); // enable if you want logs

  // ensure both down at startup
  headUp[0] = false;
  headUp[1] = false;
  startReset();
}

// ===================== BUTTON HANDLER =====================
// Returns event codes:
// 0 = none, 1 = single, 2 = double, 3 = triple, 4 = quad, 5 = long hold (reset)
int checkButton() {
  bool reading = digitalRead(buttonPin);

  if (reading != lastReading) lastDebounceTime = millis();

  if (millis() - lastDebounceTime > DEBOUNCE_MS) {
    if (reading != stableState) {
      stableState = reading;
      if (stableState == LOW) {
        pressTime = millis();
        longHoldFired = false;
      } else { // release
        if (!longHoldFired) {
          clickCount++;
          lastReleaseTime = millis();
          waitingForClicks = true;
        }
      }
    }
  }

  lastReading = reading;

  // long-hold detection (immediate)
  if (stableState == LOW && !longHoldFired && (millis() - pressTime) >= LONG_HOLD_TIME) {
    longHoldFired = true;
    clickCount = 0;
    waitingForClicks = false;
    return 5;
  }

  // multi-click timeout
  if (waitingForClicks && (millis() - lastReleaseTime) >= CLICK_TIMEOUT) {
    int c = clickCount;
    clickCount = 0;
    waitingForClicks = false;
    if (c <= 0) return 0;
    if (c == 1) return 1;
    if (c == 2) return 2;
    if (c == 3) return 3;
    return 4; // 4 or more
  }
  return 0;
}

// ===================== MOTIONS =====================
void startBothToggle() {
  motionState = ACTIVE;
  motionStart = millis();

  // toggle both individually
  if (!headUp[0] || !headUp[1]) {
    // if either is down, move both up
    if (!headUp[0]) pwmBegin(leftupPin);
    if (!headUp[1]) pwmBegin(rightupPin);
    // final states will be updated when motors finish
  } else {
    // both up -> move both down
    pwmBegin(leftdownPin);
    pwmBegin(rightdownPin);
  }
}

void startWink() {
  motionState = WINKING;
  motionStart = millis();
  winkStep = 1;

  // choose which side to wink
  winkSide = winkLeftNext ? 0 : 1;
  winkLeftNext = !winkLeftNext;  // alternate next time

  // snapshot original position
  winkOriginalUp = headUp[winkSide];

  // choose correct pin based on side + direction
  if (winkSide == 0) {
    // LEFT
    if (winkOriginalUp) pwmBegin(leftdownPin);
    else                pwmBegin(leftupPin);
  } else {
    // RIGHT
    if (winkOriginalUp) pwmBegin(rightdownPin);
    else                pwmBegin(rightupPin);
  }
}


void startSplit() {
  motionState = ACTIVE;
  motionStart = millis();
  splitFlipped = !splitFlipped;

  if (!splitFlipped) {
    // left up, right down
    if (!headUp[0]) pwmBegin(leftupPin);
    if (headUp[1])  pwmBegin(rightdownPin);
  } else {
    // left down, right up
    if (headUp[0])  pwmBegin(leftdownPin);
    if (!headUp[1]) pwmBegin(rightupPin);
  }
}

void startReset() {
  // move both down (ensure both are down)
  motionState = ACTIVE;
  motionStart = millis();
  if (headUp[0]) pwmBegin(leftdownPin);
  if (headUp[1]) pwmBegin(rightdownPin);
  // if they are already down, nothing starts and activeMask==0 will cause no-op
}

void queueWaveOrStart() {
  // If either head is up, bring them down first and queue the wave.
  if (headUp[0] || headUp[1]) {
    motionState = QUEUED_WAVE;
    // Start downs if needed
    if (headUp[0]) pwmBegin(leftdownPin);
    if (headUp[1]) pwmBegin(rightdownPin);
  } else {
    // Already down: start wave immediately
    motionState = WAVING;
    waveStep = 0;
    waveStepStart = millis();
    pwmBegin(waveSequence[waveStep++]); // start first wave step
  }
}

void startWave() {
  // fallback to startWave if you want immediate start (not used now)
  motionState = WAVING;
  waveStep = 0;
  waveStepStart = millis();
  pwmBegin(waveSequence[waveStep++]);
}

// ===================== MOTION UPDATE =====================
void updateMotion() {
  unsigned long now = millis();

  // update PWM outputs and detect per-channel finishes
  pwmUpdate();

  // Handle queued wave: wait until all motors finished (activeMask == 0) then start wave
  if (motionState == QUEUED_WAVE && activeMask == 0) {
    motionState = WAVING;
    waveStep = 0;
    waveStepStart = millis();
    pwmBegin(waveSequence[waveStep++]); // start first wave step
    return; 
  }

  if (motionState == WINKING) {
    uint8_t upCh   = (winkSide == 0) ? CH_LEFT_UP   : CH_RIGHT_UP;
    uint8_t downCh = (winkSide == 0) ? CH_LEFT_DOWN : CH_RIGHT_DOWN;

    bool sideActive = activeMask & (MASK(upCh) | MASK(downCh));

    if (!sideActive) {
      if (winkStep == 1) {
        // return to original position
        if (winkSide == 0) {
          if (winkOriginalUp) pwmBegin(leftupPin);
          else                pwmBegin(leftdownPin);
        } else {
          if (winkOriginalUp) pwmBegin(rightupPin);
          else                pwmBegin(rightdownPin);
        }
        winkStep = 2;
      } else {
        motionState = IDLE;
        winkStep = 0;
      }
    }
    return;
  }


  if (motionState == WAVING) {
    // start next wave step when WAVE_STEP_DELAY elapsed since last step started
    if ((now - waveStepStart) >= WAVE_STEP_DELAY) {
      // If waveStep reached end, finish
      if (waveStep >= WAVE_STEPS) {
        // ensure remaining motors finish naturally
        motionState = IDLE;
        return;
      }
      // start next step without killing previous (per-channel stop logic handles each)
      pwmBegin(waveSequence[waveStep++]);
      waveStepStart = now;
    }
    return;
  }

  // ACTIVE state: standard toggles/split/reset — finish when all motors finish
  if (motionState == ACTIVE) {
    // safety cutoff
    if ((now - motionStart) >= MAX_MOTOR_TIME) {
    pwmOffAll();
    motionState = IDLE;
      return;
  }
    // If nothing is actively driven, done
     if (activeMask == 0) {
        motionState = IDLE;
      return;
     }
  }
}

// ===================== MAIN LOOP WRAPPER =====================
void loopWrapper() {
  // kept for readability if you want to call updateMotion and PWM independently
  // (we call them in loop())
}

// ===================== MAIN LOOP =====================
void loop() {
  int evt = checkButton();

  // long hold event returned immediately by checkButton
  if (evt == 5) {
    startReset();
  } else {
    // If idle, accept new multi-click events; otherwise ignore (or you could queue)
    if (motionState == IDLE) {
      if (evt == 1) startBothToggle();
      else if (evt == 2) startWink();
      else if (evt == 3) queueWaveOrStart();
      else if (evt == 4) startSplit();
    }
  }

  // update PWM & motion handling each loop
  updateMotion();
}
