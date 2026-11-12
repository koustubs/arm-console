#include <Servo.h>

// =====================================
// SERVO OBJECTS
// =====================================

Servo baseServo;
Servo shoulderServo;
Servo elbowServo;
Servo gripperServo;

// =====================================
// PIN CONNECTIONS
// =====================================

#define BASE 3
#define SHOULDER 5
#define ELBOW 6
#define GRIPPER 9

#define TRIG 12
#define ECHO 11
#define GRIP_SWITCH 2

// =====================================
// INITIAL POSITIONS
// =====================================

const int BASE_INITIAL = 80;
const int SHOULDER_INITIAL = 180;
const int ELBOW_INITIAL = 0;

// =====================================
// SCANNING POSITIONS
// =====================================

const int SHOULDER_SCAN = 110;
const int ELBOW_SCAN = 130;

// =====================================
// APPROACH LIMITS
// =====================================

const int SHOULDER_MAX = 140;
const int ELBOW_MAX = 150;

// =====================================
// LIFTING POSITIONS
// =====================================

const int SHOULDER_LIFT = 110;
const int ELBOW_LIFT = 120;

// =====================================
// GRIPPER LIMITS
// =====================================

const int GRIPPER_CLOSE = 0;
const int GRIPPER_OPEN = 30;

const int GRIPPER_DELAY = 80;

// =====================================
// BASE SETTINGS
// =====================================

const int RIGHT_SIDE_ANGLE = 160;

// UPDATED EXTRA ROTATION
const int EXTRA_ROTATION = 8;

// =====================================
// DISTANCE SETTINGS
// =====================================

const float DETECT_DISTANCE = 20.0;
const float PICK_DISTANCE = 4.0;

// =====================================
// MOVEMENT SETTINGS
// =====================================

const int ARM_DELAY = 45;
const int BASE_DELAY = 35;

int shoulderPosition = SHOULDER_SCAN;
int elbowPosition = ELBOW_SCAN;
int basePosition = BASE_INITIAL;

unsigned long lastSensorUpdate = 0;

// =====================================
// DISTANCE FUNCTION
// =====================================

float getDistance() {

  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG, LOW);

  unsigned long duration =
    pulseIn(ECHO, HIGH, 30000);

  if (duration == 0) {
    return -1;
  }

  return duration * 0.0343 / 2.0;
}

// =====================================
// SEND LIVE DATA TO PYTHON
// =====================================

void sendSensorData() {

  float distance = getDistance();

  if (distance >= 0) {

    Serial.print("DISTANCE: ");
    Serial.print(distance, 1);
    Serial.println(" CM");

  } else {

    Serial.println("DISTANCE: -1");
  }

  if (digitalRead(GRIP_SWITCH) == LOW) {

    Serial.println("GRIP:1");

  } else {

    Serial.println("GRIP:0");
  }
}

// =====================================
// CONTINUOUS SENSOR MONITORING
// =====================================

void monitorSensors() {

  if (millis() - lastSensorUpdate >= 150) {

    sendSensorData();

    lastSensorUpdate = millis();
  }
}

void monitoredDelay(unsigned long duration) {

  unsigned long startTime = millis();

  while (millis() - startTime < duration) {

    monitorSensors();

    delay(5);
  }
}

// =====================================
// GRIPPER CONTROL
// =====================================

void setGripper(int angle) {

  angle = constrain(angle, 0, 30);

  gripperServo.write(angle);
}

void smoothGripperClose() {

  Serial.println("GRIPPER CLOSING");

  for (int angle = GRIPPER_OPEN;
       angle >= GRIPPER_CLOSE;
       angle--) {

    setGripper(angle);

    monitoredDelay(GRIPPER_DELAY);
  }
}

// =====================================
// SMOOTH SERVO MOVEMENT
// =====================================

void smoothMove(
  Servo &servo,
  int &currentAngle,
  int targetAngle,
  int movementDelay
) {

  targetAngle = constrain(targetAngle, 0, 180);

  while (currentAngle != targetAngle) {

    if (currentAngle < targetAngle) {
      currentAngle++;
    } else {
      currentAngle--;
    }

    servo.write(currentAngle);

    monitoredDelay(movementDelay);
  }
}

// =====================================
// OBJECT DETECTION
// =====================================

bool objectDetected() {

  int count = 0;

  for (int i = 0; i < 3; i++) {

    float distance = getDistance();

    if (distance >= 2 &&
        distance <= DETECT_DISTANCE) {

      count++;
    }

    monitorSensors();

    monitoredDelay(60);
  }

  return count >= 2;
}

// =====================================
// APPROACH OBJECT
// =====================================

void approachObject() {

  while (shoulderPosition < SHOULDER_MAX ||
         elbowPosition < ELBOW_MAX) {

    float distance = getDistance();

    if (distance >= 2 &&
        distance <= PICK_DISTANCE) {

      Serial.println("PICK DISTANCE REACHED");

      return;
    }

    if (distance < 0) {

      monitoredDelay(150);
      continue;
    }

    if (shoulderPosition < SHOULDER_MAX) {

      shoulderPosition++;

      shoulderServo.write(shoulderPosition);
    }

    if (elbowPosition < ELBOW_MAX) {

      elbowPosition++;

      elbowServo.write(elbowPosition);
    }

    monitoredDelay(120);
  }

  Serial.println("APPROACH LIMIT REACHED");
}

// =====================================
// PICK AND PLACE
// =====================================

void pickAndPlace() {

  Serial.println("OBJECT DETECTED");

  // OPEN GRIPPER

  setGripper(GRIPPER_OPEN);

  monitoredDelay(800);

  // APPROACH

  Serial.println("APPROACHING OBJECT");

  approachObject();

  // SLOW GRIPPER CLOSING

  smoothGripperClose();

  monitoredDelay(1000);

  Serial.println("GRIPPER CLOSED");

  // LIFT ELBOW

  smoothMove(
    elbowServo,
    elbowPosition,
    ELBOW_LIFT,
    ARM_DELAY
  );

  monitoredDelay(500);

  // LIFT SHOULDER

  smoothMove(
    shoulderServo,
    shoulderPosition,
    SHOULDER_LIFT,
    ARM_DELAY
  );

  monitoredDelay(1000);

  // ROTATE TO RIGHT

  smoothMove(
    baseServo,
    basePosition,
    RIGHT_SIDE_ANGLE,
    BASE_DELAY
  );

  monitoredDelay(1500);

  // RELEASE OBJECT

  setGripper(GRIPPER_OPEN);

  monitoredDelay(1500);

  // RETURN SHOULDER

  smoothMove(
    shoulderServo,
    shoulderPosition,
    SHOULDER_SCAN,
    ARM_DELAY
  );

  // RETURN ELBOW

  smoothMove(
    elbowServo,
    elbowPosition,
    ELBOW_SCAN,
    ARM_DELAY
  );

  // RETURN BASE

  smoothMove(
    baseServo,
    basePosition,
    BASE_INITIAL,
    BASE_DELAY
  );

  // RESET

  setGripper(GRIPPER_OPEN);

  monitoredDelay(500);

  shoulderPosition = SHOULDER_SCAN;
  elbowPosition = ELBOW_SCAN;
  basePosition = BASE_INITIAL;

  Serial.println("CYCLE COMPLETE");
}

// =====================================
// SETUP
// =====================================

void setup() {

  Serial.begin(9600);

  baseServo.attach(BASE);
  shoulderServo.attach(SHOULDER);
  elbowServo.attach(ELBOW);
  gripperServo.attach(GRIPPER);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  pinMode(GRIP_SWITCH, INPUT_PULLUP);

  digitalWrite(TRIG, LOW);

  // INITIAL POSITION

  baseServo.write(BASE_INITIAL);
  shoulderServo.write(SHOULDER_INITIAL);
  elbowServo.write(ELBOW_INITIAL);

  setGripper(GRIPPER_OPEN);

  monitoredDelay(5000);

  // SCANNING POSITION

  shoulderServo.write(SHOULDER_SCAN);
  elbowServo.write(ELBOW_SCAN);

  shoulderPosition = SHOULDER_SCAN;
  elbowPosition = ELBOW_SCAN;
  basePosition = BASE_INITIAL;

  monitoredDelay(2000);
}

// =====================================
// MAIN SCANNING LOOP
// =====================================

void loop() {

  // LEFT TO RIGHT

  for (int angle = 0; angle <= 180; angle += 2) {

    baseServo.write(angle);
    basePosition = angle;

    monitoredDelay(60);

    if (objectDetected()) {

      Serial.println("OBJECT DETECTED - MOVING 8 DEGREES");

      int targetAngle = min(
        angle + EXTRA_ROTATION,
        180
      );

      smoothMove(
        baseServo,
        basePosition,
        targetAngle,
        BASE_DELAY
      );

      monitoredDelay(500);

      pickAndPlace();

      return;
    }
  }

  // RIGHT TO LEFT

  for (int angle = 180; angle >= 0; angle -= 2) {

    baseServo.write(angle);
    basePosition = angle;

    monitoredDelay(60);

    if (objectDetected()) {

      Serial.println("OBJECT DETECTED - MOVING 8 DEGREES");

      int targetAngle = max(
        angle - EXTRA_ROTATION,
        0
      );

      smoothMove(
        baseServo,
        basePosition,
        targetAngle,
        BASE_DELAY
      );

      monitoredDelay(500);

      pickAndPlace();

      return;
    }
  }
}
