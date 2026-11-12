#include <Servo.h>

// Arduino Uno / original project pinout. Angles are commands, not feedback.
const byte BASE_PIN = 3, SHOULDER_PIN = 5, ELBOW_PIN = 6, GRIP_PIN = 9;
const byte TRIG_PIN = 12, ECHO_PIN = 11, SWITCH_PIN = 2;
const unsigned long SAMPLE_MS = 150, APPROACH_TIMEOUT_MS = 15000;
const byte INVALID_LIMIT = 10;
Servo baseServo, shoulderServo, elbowServo, gripServo;
int baseAngle = 80, shoulderAngle = 180, elbowAngle = 0, gripAngle = 30;
enum State { IDLE, PREPARE, SCAN, ALIGN, OPENING, APPROACH, CLOSE_GRIP,
             LIFT_ELBOW, LIFT_SHOULDER, TRANSFER, RELEASE, RETURN_SHOULDER,
             RETURN_ELBOW, RETURN_BASE, COMPLETE, FAULT };
State state = IDLE;
const char *stateName() {
  switch (state) {
    case IDLE: return "IDLE"; case PREPARE: return "PREPARE";
    case SCAN: return "SCAN"; case ALIGN: return "ALIGN";
    case OPENING: return "OPENING"; case APPROACH: return "APPROACH";
    case CLOSE_GRIP: return "CLOSE_GRIP"; case LIFT_ELBOW: return "LIFT_ELBOW";
    case LIFT_SHOULDER: return "LIFT_SHOULDER"; case TRANSFER: return "TRANSFER";
    case RELEASE: return "RELEASE"; case RETURN_SHOULDER: return "RETURN_SHOULDER";
    case RETURN_ELBOW: return "RETURN_ELBOW"; case RETURN_BASE: return "RETURN_BASE";
    case COMPLETE: return "COMPLETE"; default: return "FAULT";
  }
}
unsigned long lastSample = 0, lastStep = 0, stateAt = 0, sequence = 0;
float distanceCm = -1;
byte invalidCount = 0, votes = 0, samplesAtPosition = 0;
int direction = 1, alignTarget = 0;
bool attached = false;
char command[16]; byte commandLength = 0; bool commandOverflow = false;

void changeState(State next) {
  state = next; stateAt = millis(); lastStep = stateAt;
  Serial.print(F("E,")); Serial.print(stateAt); Serial.print(','); Serial.println(stateName());
}
void fault(const __FlashStringHelper *reason) {
  changeState(FAULT); Serial.print(F("F,")); Serial.println(reason);
}
void attachServos() {
  if (attached) return;
  // Establish the same initial commands as the baseline before attaching.
  baseAngle = 80; shoulderAngle = 180; elbowAngle = 0; gripAngle = 30;
  baseServo.write(baseAngle); shoulderServo.write(shoulderAngle);
  elbowServo.write(elbowAngle); gripServo.write(gripAngle);
  baseServo.attach(BASE_PIN); shoulderServo.attach(SHOULDER_PIN);
  elbowServo.attach(ELBOW_PIN); gripServo.attach(GRIP_PIN); attached = true;
}
bool stepServo(Servo &servo, int &angle, int target, unsigned long interval) {
  if (angle == target) return true;
  if (millis() - lastStep >= interval) {
    angle += angle < target ? 1 : -1; servo.write(angle); lastStep = millis();
  }
  return angle == target;
}
void handleCommand() {
  if (!strcmp(command, "STOP")) {
    changeState(IDLE); Serial.println(F("A,STOP"));
  } else if (!strcmp(command, "START")) {
    if (state != IDLE && state != FAULT && state != COMPLETE) {
      Serial.println(F("N,BUSY")); return;
    }
    // On a restart, preserve last commanded positions; do not jump back to initial pose.
    attachServos(); votes = samplesAtPosition = invalidCount = 0; direction = 1;
    changeState(PREPARE); Serial.println(F("A,START"));
  } else if (!strcmp(command, "HELLO")) Serial.println(F("ARM,1"));
  else Serial.println(F("N,UNKNOWN"));
}
void readCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      command[commandLength] = 0;
      if (commandOverflow) Serial.println(F("N,TOO_LONG"));
      else if (commandLength) handleCommand();
      commandLength = 0; commandOverflow = false;
    } else if (c != '\r') {
      if (commandLength < sizeof(command) - 1 && !commandOverflow) command[commandLength++] = c;
      else commandOverflow = true;
    }
  }
}
void sample() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10); digitalWrite(TRIG_PIN, LOW);
  unsigned long echo = pulseIn(ECHO_PIN, HIGH, 30000);
  distanceCm = echo ? echo * 0.0343f / 2.0f : -1;
  unsigned long now = millis();
  Serial.print(F("S,")); Serial.print(sequence++); Serial.print(','); Serial.print(now);
  Serial.print(','); Serial.print(echo); Serial.print(','); Serial.print(distanceCm, 1);
  Serial.print(','); Serial.print(digitalRead(SWITCH_PIN) == LOW ? 1 : 0);
  Serial.print(','); Serial.print(stateName()); Serial.print(','); Serial.print(baseAngle);
  Serial.print(','); Serial.print(shoulderAngle); Serial.print(','); Serial.print(elbowAngle);
  Serial.print(','); Serial.println(gripAngle);
  if (state == SCAN) {
    samplesAtPosition++;
    if (distanceCm >= 2 && distanceCm <= 20) votes++;
    if (samplesAtPosition >= 3) {
      if (votes >= 2) { alignTarget = constrain(baseAngle + direction * 8, 0, 180); changeState(ALIGN); }
      else {
        if (baseAngle >= 180) direction = -1; else if (baseAngle <= 0) direction = 1;
        baseAngle = constrain(baseAngle + direction * 2, 0, 180); baseServo.write(baseAngle);
      }
      votes = samplesAtPosition = 0;
    }
  } else if (state == APPROACH) {
    if (distanceCm < 2) {
      if (++invalidCount >= INVALID_LIMIT) fault(F("RANGE_INVALID"));
    } else {
      invalidCount = 0;
      if (distanceCm <= 4) changeState(CLOSE_GRIP);
      else if (shoulderAngle >= 140 && elbowAngle >= 150) fault(F("APPROACH_LIMIT"));
      else {
        if (shoulderAngle < 140) shoulderServo.write(++shoulderAngle);
        if (elbowAngle < 150) elbowServo.write(++elbowAngle);
      }
    }
  }
}
void setup() {
  pinMode(TRIG_PIN, OUTPUT); digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT); pinMode(SWITCH_PIN, INPUT_PULLUP);
  Serial.begin(9600); Serial.println(F("ARM,1"));
}
void loop() {
  readCommands();
  unsigned long now = millis();
  if (now - lastSample >= SAMPLE_MS) { lastSample = now; sample(); }
  switch (state) {
    case PREPARE:
      if (!stepServo(shoulderServo, shoulderAngle, 110, 45)) break;
      if (!stepServo(elbowServo, elbowAngle, 130, 45)) break;
      if (!stepServo(gripServo, gripAngle, 30, 80)) break;
      changeState(SCAN); break;
    case ALIGN: if (stepServo(baseServo, baseAngle, alignTarget, 35)) changeState(OPENING); break;
    case OPENING:
      if (stepServo(gripServo, gripAngle, 30, 80) && millis() - stateAt >= 800) {
        invalidCount = 0; changeState(APPROACH);
      } break;
    case APPROACH: if (millis() - stateAt >= APPROACH_TIMEOUT_MS) fault(F("APPROACH_TIMEOUT")); break;
    case CLOSE_GRIP: if (stepServo(gripServo, gripAngle, 0, 80)) changeState(LIFT_ELBOW); break;
    case LIFT_ELBOW: if (stepServo(elbowServo, elbowAngle, 120, 45)) changeState(LIFT_SHOULDER); break;
    case LIFT_SHOULDER: if (stepServo(shoulderServo, shoulderAngle, 110, 45)) changeState(TRANSFER); break;
    case TRANSFER: if (stepServo(baseServo, baseAngle, 160, 35)) changeState(RELEASE); break;
    case RELEASE: if (stepServo(gripServo, gripAngle, 30, 80)) changeState(RETURN_SHOULDER); break;
    case RETURN_SHOULDER: if (stepServo(shoulderServo, shoulderAngle, 110, 45)) changeState(RETURN_ELBOW); break;
    case RETURN_ELBOW: if (stepServo(elbowServo, elbowAngle, 130, 45)) changeState(RETURN_BASE); break;
    case RETURN_BASE: if (stepServo(baseServo, baseAngle, 80, 35)) changeState(COMPLETE); break;
    default: break;
  }
}
