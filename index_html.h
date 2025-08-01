const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%TITLE%</title>
</head>
<body style="background:#111;color:#eee;font-family:Arial;text-align:center;padding:20px;">
<h1>%TITLE%</h1>
<div>
  <input type="range" min="0" max="10" value="5" id="modeSlider" style="width:200px;" list="tickmarks">
  <datalist id="tickmarks">
    <option value="0" label="0">
    <option value="1" label="1">
    <option value="2" label="2">
    <option value="3" label="3">
    <option value="4" label="4">
    <option value="5" label="5">
    <option value="6" label="6">
    <option value="7" label="7">
    <option value="8" label="8">
    <option value="9" label="9">
    <option value="10" label="10">
  </datalist>
</div>
<div>Pokaz: <span id="modeName">Loading...</div>
<div style="margin-top:30px;">
  <div>SOC: <span id="socValue">--</span>%</div>
  <div>Temp. wew.: <span id="tempValue">--</span>°C</div>
</div>
<script>
let lastTemp = null;
let lastSOC = null;
let lastMode = null;
let userChanging = false;
const slider = document.getElementById("modeSlider");
const modeLabel = document.getElementById("modeName");
slider.addEventListener("input", function () {
  userChanging = true;
  fetch("/setMode?mode=" + slider.value)
    .then(r => r.text())
    .then(data => {
      console.log("Set mode result:", data);
      fetch("/getStatus")
        .then(r => r.json())
        .then(d => {
          modeLabel.textContent = d.modeName;
          lastMode = d.mode;
        });
      userChanging = false;
    })
    .catch(e => {
      console.error("Set mode error:", e);
      userChanging = false;
    });
});
function updateAll() {
  fetch("/getStatus")
    .then(r => r.json())
    .then(data => {
      if (data.temp !== lastTemp) {
        document.getElementById("tempValue").textContent = data.temp;
        lastTemp = data.temp;
      }
      if (data.soc !== lastSOC) {
        document.getElementById("socValue").textContent = data.soc;
        lastSOC = data.soc;
      }
      if (!userChanging && data.mode !== lastMode) {
        slider.value = data.mode;
        modeLabel.textContent = data.modeName;
        lastMode = data.mode;
      }
    })
    .catch(e => console.error("Status fetch error:", e));
}
setInterval(updateAll, 2000);
updateAll();
</script>
</body>
</html>
)rawliteral";