#include <WebServer.h>
#include <WiFi.h>

#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

// ===== WIFI CREDENTIALS =====
const char *ssid = "MS";
const char *password = "987654321";

// ===== STATIC IP CONFIGURATION =====
IPAddress local_IP(10, 116, 8, 100);
IPAddress gateway(10, 116, 8, 93);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// ===== SERVER =====
WebServer server(80);

// ===== STATE =====
String currentStatus = "System ready"; // Human readable status
String currentPosition = "ORIGIN";
String unparsedData = "F:-- R:--"; // Raw sensor string
bool isFire = false;
bool isObstacle = false;
bool isMoving = false;

// ===== COMPLETE DASHBOARD HTML =====
const char dashboard_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <title>Guide Robot</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <style>
    :root { --bg: #0b0f1f; --panel: rgba(255,255,255,0.08); --border: rgba(255,255,255,0.18); --text: #eaeaff; --accent: #7f5cff; }
    * { box-sizing: border-box; font-family: "Segoe UI", sans-serif; }
    body { margin: 0; background-color: var(--bg); color: var(--text); height: 100vh; overflow: hidden; }
    .dashboard { display: grid; grid-template-rows: 60px 1fr; height: 100vh; padding: 12px; gap: 12px; }
    .header { background: var(--panel); border: 1px solid var(--border); border-radius: 14px; display: flex; align-items: center; justify-content: center; font-size: 20px; font-weight: 600; }
    .main { display: grid; grid-template-columns: 320px 1fr; grid-template-rows: auto 1fr 140px; gap: 12px; height: 100%; min-height: 0; }
    .panel { background: var(--panel); border: 1px solid var(--border); border-radius: 14px; padding: 12px; overflow: hidden; }
    .panel-title { font-size: 14px; font-weight: 600; margin-bottom: 8px; }
    .control-panel { grid-column: 1/2; grid-row: 1/2; display: flex; flex-direction: column; gap: 10px; }
    .control-panel button, .control-panel select { width: 100%; padding: 12px; background: rgba(30,30,60,0.9); border: 1px solid var(--border); border-radius: 8px; color: var(--text); cursor: pointer; }
    .go { background: linear-gradient(90deg, #4dd2ff, #7f5cff); border: none; font-weight: bold; }
    .go:disabled { opacity: 0.5; }
    .stop { background: linear-gradient(90deg, #ffb347, #ff7043); border: none; font-weight: bold; }
    .map { grid-column: 2/3; grid-row: 1/3; position: relative; width: 100%; height: 100%; }
    .fire { grid-column: 1/2; grid-row: 2/3; display: flex; flex-direction: column; gap: 8px; }
    .fire-status-container { flex: 1; display: flex; flex-direction: column; gap: 8px; }
    .fire-status-box { flex: 1; display: flex; align-items: center; justify-content: center; background: rgba(0,0,0,0.35); border-radius: 10px; border: 2px solid transparent; transition: all 0.3s ease; }
    .fire-status-box.active { border-color: #ff4444; background: rgba(255,68,68,0.15); }
    .fire-status-box.safe { border-color: #44ff44; background: rgba(68,255,68,0.1); }
    .fire-status-content { text-align: center; }
    .fire-status-icon { font-size: 32px; margin-bottom: 5px; }
    .fire-status-text { font-size: 16px; font-weight: bold; }
    .command-status { font-size: 14px; grid-column: 1/2; grid-row: 3/4; display: flex; flex-direction: column; }
    .status-log { margin-top: 8px; flex: 1; overflow-y: auto; padding: 8px; background: rgba(0,0,0,0.35); border-radius: 8px; font-size: 13px; font-family: monospace; }
    .sensor-status { grid-column: 2/3; grid-row: 3/4; display: grid; grid-template-columns: repeat(4, 1fr); gap: 8px; }
    .sensor-box { background: rgba(255,255,255,0.12); border: 1px solid var(--border); border-radius: 8px; padding: 8px; display: flex; align-items: center; justify-content: center; font-size: 18px; font-weight: bold; transition: all 0.3s ease; }
    .sensor-box.warning { background: rgba(255,140,0,0.25); border-color: #ff8c00; color: #ff8c00; }
  </style>
</head>
<body>
  <div class="dashboard">
    <div class="header">Indoor Guidance Robot</div>
    <div class="main">
      <div class="panel control-panel">
        <div class="panel-title">Control Panel</div>
        <select id="destinationSelect">
          <option value="ORIGIN">ORIGIN</option>
          <option value="LAB">LAB</option>
          <option value="OFFICE">OFFICE</option>
          <option value="CANTEEN">CANTEEN</option>
        </select>
        <button class="go" id="goBtn">GO</button>
        <button class="stop" id="stopBtn">STOP</button>
      </div>

      <div class="panel map">
        <svg width="100%" height="100%" viewBox="0 0 1000 520">
          <line x1="142" y1="100" x2="858" y2="100" stroke="black" stroke-width="4"/>
          <line x1="500" y1="100" x2="500" y2="400" stroke="black" stroke-width="4"/>
          <line x1="100" y1="142" x2="100" y2="400" stroke="black" stroke-width="4"/>
          <line x1="100" y1="400" x2="458" y2="400" stroke="black" stroke-width="4"/>
          <line x1="542" y1="400" x2="858" y2="400" stroke="black" stroke-width="4"/>
          <line x1="900" y1="142" x2="900" y2="358" stroke="black" stroke-width="4"/>
          <circle cx="100" cy="100" r="40" fill="#7f5cff" stroke="black" stroke-width="4"/>
          <circle cx="900" cy="100" r="40" fill="#7f5cff" stroke="black" stroke-width="4"/>
          <circle cx="900" cy="400" r="40" fill="#7f5cff" stroke="black" stroke-width="4"/>
          <circle cx="500" cy="400" r="40" fill="#7f5cff" stroke="black" stroke-width="4"/>
          <text x="100" y="105" text-anchor="middle" font-weight="bold">Origin</text>
          <text x="900" y="105" text-anchor="middle" font-weight="bold">Lab</text>
          <text x="900" y="405" text-anchor="middle" font-weight="bold">Office</text>
          <text x="500" y="405" text-anchor="middle" font-weight="bold">Canteen</text>
          <circle id="robot" cx="100" cy="100" r="15" fill="#ff3b6a" stroke="#ffffff" stroke-width="3">
            <animate attributeName="r" values="15;18;15" dur="1s" repeatCount="indefinite" />
          </circle>
        </svg>
      </div>

      <div class="panel fire">
        <b>Fire Detection Status</b>
        <div class="fire-status-container">
          <div class="fire-status-box safe" id="fireDetectedBox">
            <div class="fire-status-content">
              <div class="fire-status-icon">🔥</div>
              <div class="fire-status-text">FIRE DETECTED</div>
            </div>
          </div>
          <div class="fire-status-box safe" id="noFireBox">
            <div class="fire-status-content">
              <div class="fire-status-icon">✓</div>
              <div class="fire-status-text">NO FIRE</div>
            </div>
          </div>
        </div>
      </div>

      <div class="panel command-status">
        <b>Status Log</b>
        <div class="status-log" id="statusLog"></div>
        <div id="stateText" style="margin-top: 5px; font-weight: bold;">State: READY</div>
      </div>

      <div class="panel sensor-status">
        <div class="sensor-box" id="frontVal">F: --</div>
        <div class="sensor-box" id="rearVal">R: --</div>
        <div class="sensor-box" id="obstacleVal">Obstacle: OK</div>
        <div class="sensor-box" id="personVal">Ppl: OK</div>
      </div>
    </div>
  </div>

  <script>
    // ================= ROUTES & NODES =================
    var NODES = {
      ORIGIN: {x:100,y:100}, LAB: {x:900,y:100}, OFFICE: {x:900,y:400}, CANTEEN: {x:500,y:400},
      MID_TOP: {x:500,y:100}, CORNER_BL: {x:100,y:400} 
    };
    var ROUTES = {
      ORIGIN: { LAB: ["ORIGIN","MID_TOP","LAB"], OFFICE: ["ORIGIN","MID_TOP","LAB","OFFICE"], CANTEEN: ["ORIGIN","CORNER_BL","CANTEEN"], ORIGIN:["ORIGIN"]},
      LAB: { ORIGIN: ["LAB","MID_TOP","ORIGIN"], OFFICE: ["LAB","OFFICE"], CANTEEN: ["LAB","OFFICE","CANTEEN"], LAB:["LAB"]},
      OFFICE: { ORIGIN: ["OFFICE","CANTEEN","CORNER_BL","ORIGIN"], LAB: ["OFFICE","LAB"], CANTEEN: ["OFFICE","CANTEEN"], OFFICE:["OFFICE"]},
      CANTEEN: { ORIGIN: ["CANTEEN","CORNER_BL","ORIGIN"], LAB: ["CANTEEN","OFFICE","LAB"], OFFICE: ["CANTEEN","OFFICE"], CANTEEN:["CANTEEN"]}
    };

    var currentLocation = "ORIGIN";
    var isMoving = false;
    var lastStatus = "";
    var activeLines = [];
    var startLocation = "ORIGIN";

    var goBtn = document.getElementById("goBtn");
    var stopBtn = document.getElementById("stopBtn");
    var destSelect = document.getElementById("destinationSelect");
    var statusLog = document.getElementById("statusLog");
    var stateText = document.getElementById("stateText");
    var robotEl = document.getElementById("robot");
    var fireDetectedBox = document.getElementById("fireDetectedBox");
    var noFireBox = document.getElementById("noFireBox");
    var svgEl = document.querySelector("svg");
    
    // Sensor Els
    var fVal = document.getElementById("frontVal");
    var rVal = document.getElementById("rearVal");
    var obstacleVal = document.getElementById("obstacleVal");
    var pVal = document.getElementById("personVal");

    function log(msg) {
      var line = document.createElement("div");
      line.textContent = "> " + msg;
      statusLog.appendChild(line);
      statusLog.scrollTop = statusLog.scrollHeight;
      stateText.innerText = "State: " + msg;
    }

    goBtn.onclick = function() {
      var dest = destSelect.value;
      if (dest === currentLocation) { log("Already at " + dest); return; }
      if (isMoving) { log("Already moving"); return; }
      var map = { "ORIGIN":"O", "LAB":"L", "OFFICE":"F", "CANTEEN":"C" };
      startLocation = currentLocation;
      isMoving = true;
      fetch("/command?cmd=GO " + map[dest]);
    };
    
    stopBtn.onclick = function() {
      isMoving = false;
      fetch("/command?cmd=STOP");
    };

    // POLLING
    setInterval(function() {
      fetch("/status")
        .then(function(res) { return res.json(); })
        .then(function(data) {
          // SYNC: Always update currentLocation from server
          if (data.position && data.position !== currentLocation) {
            var newLoc = data.position;
            if (ROUTES[startLocation] && ROUTES[startLocation][newLoc]) {
              animateRobot(ROUTES[startLocation][newLoc]);
            }
            currentLocation = newLoc;
            isMoving = false;
            var pos = NODES[currentLocation];
            if (pos) {
              robotEl.setAttribute("cx", pos.x);
              robotEl.setAttribute("cy", pos.y);
            }
          }

          // 1. Log Status Changes
          if (data.status !== lastStatus) {
            log(data.status);
            lastStatus = data.status;

            if (data.status === "Start guiding") {
               startLocation = currentLocation;
               isMoving = true;
            }
            
            // Update person status
            if (data.status === "Person lost (Waiting)") {
               pVal.innerText = "Ppl: LOST";
               pVal.style.color = "orange";
            } else if (data.status === "Resuming" || data.status === "Start guiding" || data.status === "Destination reached") {
               pVal.innerText = "Ppl: OK";
               pVal.style.color = "white";
            }
          }

          // 2. Update Sensor Displays
          if(data.sensors) {
             var parts = data.sensors.split(' ');
             if(parts.length >= 2) {
                 var frontMatch = parts[0].match(/F:(\d+)/);
                 var rearMatch = parts[1].match(/R:(\d+)/);
                 
                 var frontDist = 999;
                 var rearDist = 999;
                 
                 if (frontMatch) frontDist = parseInt(frontMatch[1]);
                 if (rearMatch) rearDist = parseInt(rearMatch[1]);
                 
                 if (frontDist >= 999) {
                     fVal.innerText = "F: --";
                 } else {
                     fVal.innerText = "F: " + frontDist + "cm";
                 }
                 
                 if (rearDist >= 999) {
                     rVal.innerText = "R: --";
                 } else {
                     rVal.innerText = "R: " + rearDist + "cm";
                 }
             }
          }

          // 3. Fire Status - Update boxes
          if (data.fire) {
              fireDetectedBox.className = "fire-status-box active";
              noFireBox.className = "fire-status-box";
          } else {
              fireDetectedBox.className = "fire-status-box";
              noFireBox.className = "fire-status-box safe";
          }
          
          // 4. OBSTACLE STATUS - Use boolean flag from ESP32 (most reliable!)
          // This is set by EVENT:OBSTACLE_ON/OFF from Arduino
          if (data.obstacle) {
              obstacleVal.innerText = "⚠ OBSTACLE!";
              obstacleVal.className = "sensor-box warning";
          } else {
              obstacleVal.innerText = "Obstacle: OK";
              obstacleVal.className = "sensor-box";
          }
        })
        .catch(function(e) {});
    }, 1000);

    function animateRobot(path) {
       if(!path || path.length === 0) return;
       drawPath(path);
       var end = NODES[path[path.length-1]];
       if(end) {
         robotEl.setAttribute("cx", end.x);
         robotEl.setAttribute("cy", end.y);
       }
       setTimeout(function() {
           for(var i=0; i<activeLines.length; i++) {
             activeLines[i].remove();
           }
           activeLines = [];
       }, 3000);
    }

    function drawPath(nodeNames) {
      for(var i=0; i<activeLines.length; i++) {
        activeLines[i].remove();
      }
      activeLines = [];

      for (var i = 0; i < nodeNames.length - 1; i++) {
        var startNode = NODES[nodeNames[i]];
        var endNode = NODES[nodeNames[i + 1]];
        
        if(!startNode || !endNode) continue;

        var x1 = startNode.x;
        var y1 = startNode.y;
        var x2 = endNode.x;
        var y2 = endNode.y;

        var line = document.createElementNS("http://www.w3.org/2000/svg", "line");
        line.setAttribute("x1", x1);
        line.setAttribute("y1", y1);
        line.setAttribute("x2", x2);
        line.setAttribute("y2", y2);
        line.setAttribute("stroke", "#7f5cff");
        line.setAttribute("stroke-width", "6");
        line.setAttribute("stroke-linecap", "round");
        line.setAttribute("opacity", "0.8");

        svgEl.insertBefore(line, robotEl);
        activeLines.push(line);
      }
    }
  </script>
</body>
</html>
)rawliteral";

void handleRoot() { server.send_P(200, "text/html", dashboard_html); }

void handleCommand() {
  if (server.hasArg("cmd")) {
    String cmd = server.arg("cmd");
    Serial.println(cmd); // Send to UNO
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing cmd");
  }
}

void handleStatusRoute() {
  // Build JSON with position for UI sync
  String json = "{";
  json += "\"status\":\"" + currentStatus + "\",";
  json += "\"position\":\"" + currentPosition + "\",";
  json += "\"moving\":" + String(isMoving ? "true" : "false") + ",";
  json += "\"fire\":" + String(isFire ? "true" : "false") + ",";
  json += "\"obstacle\":" + String(isObstacle ? "true" : "false") + ",";
  json += "\"sensors\":\"" + unparsedData + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);

  pinMode(4, OUTPUT);   // FIX
  digitalWrite(4, LOW); // FIX

  // REMOVE brownout disable ❌
  // WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  WiFi.begin(ssid, password);

  server.on("/", handleRoot);
  server.on("/status", handleStatusRoute);
  server.on("/command", handleCommand);
  server.begin();
}

void loop() {
  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();

    if (msg.startsWith("STATUS:")) {
      currentStatus = msg.substring(7);
      currentStatus.trim();
    } else if (msg.startsWith("LOCATION:")) {
      // Parse location: O=ORIGIN, L=LAB, F=OFFICE, C=CANTEEN
      char loc = msg.charAt(9);
      if (loc == 'O')
        currentPosition = "ORIGIN";
      else if (loc == 'L')
        currentPosition = "LAB";
      else if (loc == 'F')
        currentPosition = "OFFICE";
      else if (loc == 'C')
        currentPosition = "CANTEEN";
      isMoving = false; // Arrived, not moving anymore
    } else if (msg.startsWith("DATA")) {
      unparsedData = msg.substring(5);
      unparsedData.trim();
    } else if (msg.startsWith("EVENT:")) {
      if (msg.indexOf("FIRE_ON") >= 0)
        isFire = true;
      if (msg.indexOf("FIRE_OFF") >= 0)
        isFire = false;
      if (msg.indexOf("OBSTACLE_ON") >= 0)
        isObstacle = true;
      if (msg.indexOf("OBSTACLE_OFF") >= 0)
        isObstacle = false;
    }
  }

  server.handleClient();

  yield(); // Watchdog feed only, no delay
}
