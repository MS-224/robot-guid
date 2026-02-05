// Arduino Uno - Web-Controlled Navigation Robot
// NON-BLOCKING STATE MACHINE VERSION
// Strict compliance with "Exact Fix Plan" - FINAL DEMO READY

// ===== PINS (UNCHANGED) =====
#define ENA 3
#define IN1 5
#define IN2 6
#define ENB 11
#define IN3 9
#define IN4 10
#define BUZZER 12

#define FRONT_TRIG 4
#define FRONT_ECHO 7
#define REAR_TRIG 8
#define REAR_ECHO A0
#define FIRE_PIN A1

// ===== TUNING =====
// 1000ms = 1 "Unit" of movement roughly
#define ONE_M 1000
// 560ms = 90 degree turn roughly
#define TURN_90 560
// Speed
#define STRAIGHT_L 170
#define STRAIGHT_R 195
#define TURN_SPEED 170

// Obstacle/Person Detection Thresholds
#define OBSTACLE_THRESHOLD 20 // Front sensor: < 20cm = obstacle
#define PERSON_THRESHOLD 50   // Rear sensor: > this = person lost

// ===== STATE ENUMS =====
enum MotionType {
  MOTION_IDLE,
  MOTION_FORWARD,
  MOTION_BACKWARD,
  MOTION_LEFT,
  MOTION_RIGHT,
  MOTION_STOPPED
};

// ===== GLOBALS =====
MotionType currentMotion = MOTION_IDLE;
MotionType pausedMotion = MOTION_IDLE;
bool motionPaused = false;
unsigned long motionStart = 0;
unsigned long motionDuration = 0;
unsigned long remainingDuration = 0; // FIX 2: Exact resume timing
bool pausedByFire = false;
bool pausedByObstacle = false;
bool motorsStopped =
    true; // Track if motors are already stopped (start stopped)

// Sensor Globals (Updated every loop)
long frontDist = 999;
long rearDist = 999;
int fireState = HIGH; // HIGH = No Fire, LOW = Fire

// Route State
char currentLocation = 'O';
char targetDestination = ' ';
int routeStep = 0;
// We use a simplified route ID system
enum RouteID {
  ROUTE_NONE,
  ROUTE_O_L,
  ROUTE_O_F,
  ROUTE_O_C,
  ROUTE_L_O,
  ROUTE_L_F,
  ROUTE_L_C,
  ROUTE_F_O,
  ROUTE_F_L,
  ROUTE_F_C,
  ROUTE_C_O,
  ROUTE_C_L,
  ROUTE_C_F
};
RouteID currentRouteID = ROUTE_NONE;

unsigned long lastSensorReport = 0;
const int REPORT_INTERVAL = 2000; // Report every 2s

// ==========================================================
// ===== SETUP ==============================================
// ==========================================================
void setup() {
  Serial.begin(115200);

  pinMode(ENA, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENB, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  pinMode(FRONT_TRIG, OUTPUT);
  pinMode(FRONT_ECHO, INPUT);
  pinMode(REAR_TRIG, OUTPUT);
  pinMode(REAR_ECHO, INPUT);
  pinMode(FIRE_PIN, INPUT);

  delay(1000);
  Serial.println("STATUS: Robot ready (Smooth Final)");
  Serial.println("LOCATION:O"); // Tell ESP32 we are at ORIGIN
  beepOnce();
}

// ==========================================================
// ===== LOW LEVEL MOTOR CONTROL ============================
// ==========================================================
void stopMotors() {
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void moveForward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  analogWrite(ENA, STRAIGHT_L);
  analogWrite(ENB, STRAIGHT_R);
}

void moveBackward() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  analogWrite(ENA, STRAIGHT_L);
  analogWrite(ENB, STRAIGHT_R);
}

void turnLeft() { // Pivot Left
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  analogWrite(ENA, TURN_SPEED);
  analogWrite(ENB, TURN_SPEED);
}

void turnRight() { // Pivot Right
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  analogWrite(ENA, TURN_SPEED);
  analogWrite(ENB, TURN_SPEED);
}

void beepOnce() {
  digitalWrite(BUZZER, HIGH);
  delay(100);
  digitalWrite(BUZZER, LOW);
}

void beepTriple() { // Non-blocking beep logic is too complex for now, keeping
                    // simple
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER, HIGH);
    delay(100);
    digitalWrite(BUZZER, LOW);
    delay(100);
  }
}

// ==========================================================
// ===== MOTION ENGINE (STEP 3 & 4) =========================
// ==========================================================
// FIX 1: Return BOOL to control routeStep
bool startMotion(MotionType type, unsigned long duration) {
  if (currentMotion != MOTION_IDLE && currentMotion != MOTION_STOPPED)
    return false; // Did not start

  currentMotion = type;
  motionDuration = duration;
  motionStart = millis();
  motorsStopped = false; // Motors are now running

  switch (type) {
  case MOTION_FORWARD:
    moveForward();
    break;
  case MOTION_BACKWARD:
    moveBackward();
    break;
  case MOTION_LEFT:
    turnLeft();
    break;
  case MOTION_RIGHT:
    turnRight();
    break;
  default:
    stopMotors();
    motorsStopped = true;
    break;
  }
  return true; // Successfully started
}

void executeMotion() {
  // Guard: Don't execute if idle or stopped
  if (currentMotion == MOTION_IDLE || currentMotion == MOTION_STOPPED)
    return;

  // Guard: Force stop if paused (shouldn't happen normally)
  if (motionPaused) {
    if (!motorsStopped) {
      stopMotors();
      motorsStopped = true;
    }
    return;
  }

  // Check if motion timer expired
  if (millis() - motionStart >= motionDuration) {
    if (!motorsStopped) {
      stopMotors();
      motorsStopped = true;
    }
    currentMotion = MOTION_IDLE;
  }
}

void resumeMotors(MotionType type) {
  motorsStopped = false; // Motors are now running
  switch (type) {
  case MOTION_FORWARD:
    moveForward();
    break;
  case MOTION_BACKWARD:
    moveBackward();
    break;
  case MOTION_LEFT:
    turnLeft();
    break;
  case MOTION_RIGHT:
    turnRight();
    break;
  default:
    stopMotors();
    motorsStopped = true;
    break;
  }
}

// ==========================================================
// ===== SENSORS (STEP 6) ===================================
// ==========================================================
long readDist(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long duration = pulseIn(echo, HIGH, 15000); // 15ms timeout
  if (duration == 0)
    return 999;
  return duration * 0.034 / 2;
}

void readSensors() {
  frontDist = readDist(FRONT_TRIG, FRONT_ECHO);
  rearDist = readDist(REAR_TRIG, REAR_ECHO);
  fireState = digitalRead(FIRE_PIN);

  // Reporting
  if (millis() - lastSensorReport > REPORT_INTERVAL) {
    Serial.print("DATA F:");
    Serial.print(frontDist);
    Serial.print(" R:");
    Serial.print(rearDist);
    Serial.print(" FIRE:");
    Serial.println(fireState == LOW ? 1 : 0);
    lastSensorReport = millis();
  }
}

void safetySupervisor() {
  // ============================================
  // SMOOTH FIX: Only stop motors ONCE, not every loop
  // Uses motorsStopped flag to prevent jerky movement
  // ============================================
  bool frontBlocked = (frontDist < OBSTACLE_THRESHOLD && frontDist > 0);

  // Helper: Stop motors only if not already stopped
  auto safeStop = [&]() {
    if (!motorsStopped) {
      stopMotors();
      motorsStopped = true;
    }
  };

  // 1. FIRE (Highest Priority) - Stops ALL motion
  if (fireState == LOW) { // Fire detected
    safeStop();           // Stop only if running
    if (!pausedByFire) {
      pausedMotion =
          currentMotion != MOTION_IDLE && currentMotion != MOTION_STOPPED
              ? currentMotion
              : pausedMotion;
      unsigned long elapsed = millis() - motionStart;
      if (elapsed < motionDuration)
        remainingDuration = motionDuration - elapsed;
      else
        remainingDuration = 0;

      motionPaused = true;
      pausedByFire = true;
      currentMotion = MOTION_STOPPED;
      Serial.println("STATUS: Fire detected! Stopping.");
      Serial.println("EVENT: FIRE_ON");
    }
    beepOnce();
    return;
  }

  // 2. OBSTACLE CHECK (Only for FORWARD motion)
  if (currentMotion == MOTION_FORWARD && frontBlocked) {
    safeStop(); // Stop only if running
    if (!pausedByObstacle) {
      pausedMotion = currentMotion;
      unsigned long elapsed = millis() - motionStart;
      if (elapsed < motionDuration)
        remainingDuration = motionDuration - elapsed;
      else
        remainingDuration = 0;

      motionPaused = true;
      pausedByObstacle = true;
      currentMotion = MOTION_STOPPED;
      Serial.println("STATUS: Obstacle detected! Pausing.");
      Serial.println("EVENT: OBSTACLE_ON");
    }
    return; // Don't process resume while obstacle present
  }

  // Also check if paused by obstacle and obstacle still present
  if (pausedByObstacle && frontBlocked) {
    safeStop(); // Keep stopped
    return;
  }

  // 3. REAR (Person lost check - only during forward motion)
  if (!motionPaused && currentMotion == MOTION_FORWARD && rearDist > 40 &&
      rearDist < 900) {
    safeStop(); // Stop only if running
    pausedMotion = currentMotion;
    unsigned long elapsed = millis() - motionStart;
    if (elapsed < motionDuration)
      remainingDuration = motionDuration - elapsed;
    else
      remainingDuration = 0;

    motionPaused = true;
    currentMotion = MOTION_STOPPED;
    Serial.println("STATUS: Person lost (Waiting)");
    return;
  }

  // 4. RESUME - Only if paused and ALL conditions clear
  if (motionPaused) {
    bool canResume = true;

    // Check fire first
    if (fireState == LOW) {
      canResume = false;
    } else if (pausedByFire) {
      pausedByFire = false;
      Serial.println("EVENT: FIRE_OFF");
    }

    // CRITICAL FIX #1: Check obstacle - clear flag regardless of motion type
    if (frontBlocked) {
      canResume = false;
    } else if (pausedByObstacle) {
      // Clear obstacle flag even if motion type has changed
      pausedByObstacle = false;
      Serial.println("EVENT: OBSTACLE_OFF");
    }

    // Check person (rear sensor) - only if paused from forward motion
    if (pausedMotion == MOTION_FORWARD) {
      if (rearDist > 40 && rearDist < 900) {
        canResume = false;
      }
    }

    // RESUME if all clear
    if (canResume && remainingDuration > 0) {
      motionPaused = false;
      currentMotion = pausedMotion;
      motionStart = millis();
      motionDuration = remainingDuration;

      resumeMotors(pausedMotion);
      Serial.println("STATUS: Resuming");
    } else if (canResume && remainingDuration == 0) {
      // CRITICAL FIX #2: Clear ALL pause flags when completing
      motionPaused = false;
      pausedByFire = false;
      pausedByObstacle = false;
      currentMotion = MOTION_IDLE;
      Serial.println("STATUS: Step complete");
    }
  }
}

// ==========================================================
// ===== ROUTES (STEP 5 & 8) ================================
// ==========================================================
void setRoute(char dest) {
  if (dest == currentLocation) {
    Serial.println("STATUS: Already there");
    return;
  }

  routeStep = 0;

  // Mapping
  if (currentLocation == 'O') {
    if (dest == 'L')
      currentRouteID = ROUTE_O_L;
    else if (dest == 'F')
      currentRouteID = ROUTE_O_F;
    else if (dest == 'C')
      currentRouteID = ROUTE_O_C;
  } else if (currentLocation == 'L') {
    if (dest == 'O')
      currentRouteID = ROUTE_L_O;
    else if (dest == 'F')
      currentRouteID = ROUTE_L_F;
    else if (dest == 'C')
      currentRouteID = ROUTE_L_C;
  } else if (currentLocation == 'F') {
    if (dest == 'O')
      currentRouteID = ROUTE_F_O;
    else if (dest == 'L')
      currentRouteID = ROUTE_F_L;
    else if (dest == 'C')
      currentRouteID = ROUTE_F_C;
  } else if (currentLocation == 'C') {
    if (dest == 'O')
      currentRouteID = ROUTE_C_O;
    else if (dest == 'L')
      currentRouteID = ROUTE_C_L;
    else if (dest == 'F')
      currentRouteID = ROUTE_C_F;
  }

  if (currentRouteID != ROUTE_NONE) {
    targetDestination = dest;
    Serial.println("STATUS: Start guiding");
  }
}

void runRoute() {
  if (currentRouteID == ROUTE_NONE)
    return;
  if (currentMotion != MOTION_IDLE || motionPaused)
    return;
// Only proceed if IDLE and NOT paused

// Helper macro to finish route
#define FINISH_ROUTE()                                                         \
  {                                                                            \
    currentRouteID = ROUTE_NONE;                                               \
    currentLocation = targetDestination;                                       \
    Serial.print("LOCATION:");                                                 \
    Serial.println(targetDestination);                                         \
    Serial.println("STATUS: Destination reached");                             \
    beepOnce();                                                                \
    return;                                                                    \
  }

  // STATE MACHINES for each route
  // We use routeStep to advance

  // FIX 1: Only increment routeStep if startMotion returns true
  bool started = false;

  switch (currentRouteID) {
  // --- FROM ORIGIN ---
  case ROUTE_O_L: // Straight 3m
    if (routeStep == 0)
      started = startMotion(MOTION_FORWARD, 3 * ONE_M);
    else if (routeStep == 1) { // 180 turn
      started = startMotion(MOTION_RIGHT, TURN_90 * 2);
    } else
      FINISH_ROUTE();
    break;

  case ROUTE_O_F: // Lab -> Turn -> Office -> Turn
    if (routeStep == 0)
      started = startMotion(MOTION_FORWARD, 3 * ONE_M);
    else if (routeStep == 1)
      started = startMotion(MOTION_RIGHT, TURN_90);
    else if (routeStep == 2)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M);
    else if (routeStep == 3)
      started = startMotion(MOTION_RIGHT, TURN_90 * 2);
    else
      FINISH_ROUTE();
    break;

  case ROUTE_O_C: // Turn -> South -> Corner -> Turn -> East -> Canteen
    if (routeStep == 0)
      started = startMotion(MOTION_RIGHT, TURN_90);
    else if (routeStep == 1)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M);
    else if (routeStep == 2)
      started = startMotion(MOTION_LEFT, TURN_90);
    else if (routeStep == 3)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M);
    // Already facing East
    else
      FINISH_ROUTE();
    break;

  // --- FROM LAB ---
  case ROUTE_L_O: // West 3m -> Turn 180
    if (routeStep == 0)
      started = startMotion(MOTION_FORWARD, 3 * ONE_M);
    else if (routeStep == 1)
      started = startMotion(MOTION_RIGHT, TURN_90 * 2);
    else
      FINISH_ROUTE();
    break;

  case ROUTE_L_F: // Left -> Forward
    if (routeStep == 0)
      started = startMotion(MOTION_LEFT, TURN_90);
    else if (routeStep == 1)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M);
    else if (routeStep == 2)
      started = startMotion(MOTION_RIGHT, TURN_90 * 2); // Face North
    else
      FINISH_ROUTE();
    break;

  case ROUTE_L_C: // O_F logic extended? Lab->Office->Canteen
    // L->F is Left, Fwd 2. At F.
    // F->C is Left, Fwd 2.
    if (routeStep == 0)
      started = startMotion(MOTION_LEFT, TURN_90);
    else if (routeStep == 1)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M); // At F
    else if (routeStep == 2)
      started = startMotion(MOTION_RIGHT, TURN_90); // To West
    else if (routeStep == 3)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M); // At C
    else if (routeStep == 4)
      started = startMotion(MOTION_RIGHT, TURN_90 * 2); // Face East
    else
      FINISH_ROUTE();
    break;

  // --- FROM OFFICE (Facing North) ---
  case ROUTE_F_O:
    // Left (West) -> C -> Right (North) -> Mid -> Left (West) -> O
    if (routeStep == 0)
      started = startMotion(MOTION_LEFT, TURN_90);
    // ... simplified path via C & MidTop as per old code ...
    else if (routeStep == 1)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M); // At C
    else if (routeStep == 2)
      started = startMotion(MOTION_RIGHT, TURN_90); // North
    else if (routeStep == 3)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M); // MidTop
    else if (routeStep == 4)
      started = startMotion(MOTION_LEFT, TURN_90); // West
    else if (routeStep == 5)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M); // At O
    else if (routeStep == 6)
      started = startMotion(MOTION_RIGHT, TURN_90 * 2); // Face East
    else
      FINISH_ROUTE();
    break;

  case ROUTE_F_L:
    if (routeStep == 0)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M);
    else if (routeStep == 1)
      started = startMotion(MOTION_LEFT, TURN_90); // Face West
    else
      FINISH_ROUTE();
    break;

  case ROUTE_F_C:
    if (routeStep == 0)
      started = startMotion(MOTION_LEFT, TURN_90);
    else if (routeStep == 1)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M);
    else if (routeStep == 2)
      started = startMotion(MOTION_RIGHT, TURN_90 * 2);
    else
      FINISH_ROUTE();
    break;

  // --- FROM CANTEEN (Facing East) ---
  case ROUTE_C_O:
    if (routeStep == 0)
      started = startMotion(MOTION_LEFT, TURN_90); // North
    else if (routeStep == 1)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M);
    else if (routeStep == 2)
      started = startMotion(MOTION_LEFT, TURN_90); // West
    else if (routeStep == 3)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M); // At O
    else if (routeStep == 4)
      started = startMotion(MOTION_RIGHT, TURN_90 * 2); // Face East
    else
      FINISH_ROUTE();
    break;

  case ROUTE_C_L:
    if (routeStep == 0)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M); // To F
    else if (routeStep == 1)
      started = startMotion(MOTION_LEFT, TURN_90); // North
    else if (routeStep == 2)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M); // At L
    else if (routeStep == 3)
      started = startMotion(MOTION_LEFT, TURN_90); // Face West
    else
      FINISH_ROUTE();
    break;

  case ROUTE_C_F:
    if (routeStep == 0)
      started = startMotion(MOTION_FORWARD, 2 * ONE_M);
    else if (routeStep == 1)
      started = startMotion(MOTION_LEFT, TURN_90);
    else
      FINISH_ROUTE();
    break;

  default:
    currentRouteID = ROUTE_NONE;
    break;
  }

  // If we triggered a startMotion, increment step for next time
  // But wait! startMotion only sets state if IDLE.
  // So we assume if we reached here, we successfully started logic.
  // The 'startMotion' function sets currentMotion != IDLE.
  // So next loop runRoute() returns early.
  // When executeMotion finishes (IDLE), runRoute() runs again.
  // We need to verify that we *did* start something or finished.
  // We can just increment step here unconditionally because
  // runRoute() is only called when IDLE.
  if (started)
    routeStep++;
}

// ==========================================================
// ===== MAIN LOOP (STEP 7) =================================
// ==========================================================
void loop() {
  readSensors(); // Update dists & fire

  // Command Check (STEP 8)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("GO ")) {
      if (currentMotion == MOTION_IDLE && currentRouteID == ROUTE_NONE) {
        char dest = cmd.charAt(3);
        setRoute(dest);
      }
    } else if (cmd == "STOP") {
      stopMotors();
      currentMotion = MOTION_IDLE;
      currentRouteID = ROUTE_NONE;
      Serial.println("STATUS: Stopped by User");
      beepTriple();
    }
  }

  safetySupervisor(); // Pauses/Resumes motion
  executeMotion();    // Runs motors if active
  runRoute();         // Advances route if IDLE
}
