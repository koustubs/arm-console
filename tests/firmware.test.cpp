#include <cassert>
#include <iostream>
#include "Servo.h"

unsigned long fakeMillis = 0;
unsigned long fakeEcho = 0;
int fakeSwitch = HIGH;
FakeSerial Serial;
#include "../firmware/ArmController/ArmController.ino"

int main() {
  setup();
  assert(state == IDLE && !baseServo.attached && !gripServo.attached);
  assert(Serial.output.find("ARM,1\n") != std::string::npos);

  Serial.input = "START\n"; loop();
  assert(state == PREPARE && baseServo.attached && gripServo.attached);
  Serial.input = "STOP\n"; loop();
  assert(state == IDLE && Serial.output.find("A,STOP\n") != std::string::npos);
  int writes = baseServo.writes + shoulderServo.writes + elbowServo.writes + gripServo.writes;
  fakeMillis += 200; loop();
  assert(writes == baseServo.writes + shoulderServo.writes + elbowServo.writes + gripServo.writes);

  state = SCAN; baseAngle = 80; direction = 1; votes = samplesAtPosition = 0; fakeEcho = 600;
  sample(); sample(); sample();
  assert(state == ALIGN && alignTarget == 88);

  state = APPROACH; invalidCount = 0; shoulderAngle = 110; elbowAngle = 130; gripAngle = 30; fakeEcho = 0;
  for (int i = 0; i < 10; ++i) sample();
  assert(state == FAULT && gripAngle == 30);
  assert(Serial.output.find("F,RANGE_INVALID\n") != std::string::npos);

  state = APPROACH; invalidCount = 0; shoulderAngle = 110; elbowAngle = 130; fakeEcho = 1800;
  for (int i = 0; i < 10; ++i) sample();
  assert(state == FAULT && shoulderAngle == 110 && elbowAngle == 130 && gripAngle == 30);

  state = APPROACH; invalidCount = 0; shoulderAngle = 140; elbowAngle = 150; fakeEcho = 600;
  sample(); assert(state == FAULT && gripAngle == 30);
  assert(Serial.output.find("F,APPROACH_LIMIT\n") != std::string::npos);

  state = APPROACH; invalidCount = 0; shoulderAngle = 110; elbowAngle = 130; fakeEcho = 175;
  sample(); assert(state == CLOSE_GRIP);

  shoulderAngle = 140; elbowAngle = 150; baseAngle = 88; gripAngle = 30;
  fakeEcho = 600;
  for (int i = 0; i < 1000 && state != COMPLETE; ++i) { fakeMillis += 150; loop(); }
  assert(state == COMPLETE && baseAngle == 80 && shoulderAngle == 110 && elbowAngle == 130 && gripAngle == 30);

  state = APPROACH; fakeMillis += APPROACH_TIMEOUT_MS + 1; stateAt = fakeMillis - APPROACH_TIMEOUT_MS - 1;
  fakeEcho = 175; lastSample = fakeMillis - SAMPLE_MS; gripAngle = 30;
  loop(); assert(state == FAULT && gripAngle == 30);
  assert(Serial.output.find("F,APPROACH_TIMEOUT\n") != std::string::npos);

  state = IDLE; Serial.input = std::string(20, 'x') + "START\n"; loop();
  assert(state == IDLE && Serial.output.find("N,TOO_LONG\n") != std::string::npos);
  std::cout << "Firmware state and fault simulation passed.\n";
}
