static const PROGMEM char indexJS[] = R"JS("use strict";
let offsetData, pidData, satelliteData, offsetChart, pidChart, satelliteChart;

function startLoading() {
  offsetData = new google.visualization.DataTable();
  offsetData.addColumn("datetime", "gps time");
  offsetData.addColumn("number", "offset");
  offsetChart = new google.visualization.LineChart(document.getElementById('offsetChart'));

  pidData = new google.visualization.DataTable();
  pidData.addColumn("datetime", "gps time");
  pidData.addColumn("number", "D");
  pidData.addColumn("number", "P+I+D");
  pidChart = new google.visualization.LineChart(document.getElementById('pidChart'));

  satelliteData = new google.visualization.DataTable();
  satelliteData.addColumn("datetime", "gps time");
  satelliteData.addColumn("number", "Strong");
  satelliteData.addColumn("number", "Weak");
  satelliteChart = new google.visualization.LineChart(document.getElementById('satelliteChart'));

  loadClock();
}

google.charts.load('current', {'packages':['corechart']});
google.charts.setOnLoadCallback(startLoading);

const offsetOptions = {
          title: 'Clock Offset',
          legend: { position: 'bottom' },
          hAxis: {
            title: "Time"
          },
          vAxis: {
            title: "ns"
          }
        };

const pidOptions = {
          title: 'Clock Sync',
          legend: { position: 'bottom' },
          hAxis: {
            title: "Time"
          },
          vAxis: {
            title: "ppm"
          }
        };

const satelliteOptions = {
          title: 'Satellite Signal',
          legend: { position: 'bottom' },
          hAxis: {
            title: "Time"
          },
          vAxis: {
            title: "count"
          }
        };

function SNR_line_x(snr) {
  const x = 302 * snr/50;
  if(x > 302) {
    x = 302;
  }
  return x;
}

function show_radar(element) {
  const width = 355;
  const height = 315;
  const ctx = element.getContext("2d");

  // clear
  ctx.fillStyle = "rgb(0,0,0)";
  ctx.clearRect(0, 0, width, height);

  // radar circles
  ctx.strokeStyle = "rgba(0,0,0, 0.3)";
  ctx.lineWidth = 1;
  for(let radius = 150; radius >= 10; radius = radius - 46) {
    ctx.beginPath();
    ctx.arc(151, 151, radius, 0, Math.PI*2, true);
    ctx.closePath();
    ctx.stroke();
  }

  // radar cross
  ctx.beginPath();
  ctx.moveTo(0,151);
  ctx.lineTo(302,151);
  ctx.moveTo(151,0);
  ctx.lineTo(151,302);
  ctx.closePath();
  ctx.stroke();

  // SNR line
  ctx.beginPath();
  ctx.moveTo(0, 310);
  ctx.lineTo(302, 310);
  ctx.closePath();
  ctx.stroke();

  // radar "N"
  ctx.strokeStyle = "rgb(0,0,0)";
  ctx.font = "15px Georgia";
  ctx.fillText("N",152,16);

  // "0 dB" and "50 dB" for SNR line
  ctx.fillText("0 dB",10,300);
  ctx.fillText("50 dB",272,300);

  // 25 dB for SNR line
  const SNR_25 = SNR_line_x(25);
  ctx.beginPath();
  ctx.moveTo(SNR_25, 307);
  ctx.lineTo(SNR_25, 313);
  ctx.closePath();
  ctx.stroke();
}

function show_radar_sats(element, sats) {
  const ctx = element.getContext("2d");
  ctx.strokeStyle = "rgb(0,0,0)";
  ctx.lineWidth = 2;
  for(let i = 0; i < sats.length; i++) {
    const elevation_rad = (90-sats[i][1]) * Math.PI / 180; // from horizon
    const r = Math.sin(elevation_rad) * 150;

    const azimuth_rad = (540 - sats[i][2]) % 360 * Math.PI / 180; // clockwise from north
    const x = Math.sin(azimuth_rad) * r + 151;
    const y = Math.cos(azimuth_rad) * r + 151;

    // Satellite Dot on Radar
    ctx.beginPath();
    ctx.arc(x,y,5,0,Math.PI*2, true);
    ctx.closePath();
    if(sats[i][3] < 10) {
      ctx.fillStyle = "rgb(255,0,0)";
    } else if(sats[i][3] < 25) {
      ctx.fillStyle = "rgb(255,255,0)";
    } else {
      ctx.fillStyle = "rgb(0,214,7)";
    }
    ctx.fill();

    // SNR line
    const SNR_x = SNR_line_x(sats[i][3]);
    ctx.beginPath();
    ctx.arc(SNR_x, 310, 2, 0, Math.PI*2, true);
    ctx.closePath();
    ctx.fill();

    // Satellite ID
    ctx.fillStyle = "rgb(0,0,0)";
    ctx.fillText(sats[i][0],x+8,y+5);
  }
}

const lockStates = ["n/a", "no lock", "2D", "3D"];

function gotData(json) {
  $.each( json, function( key, value ) {
    if(key === "offsetHuman") {
      value = value.toFixed(9);
    } else if(key === "ppsMillis" || key === "curMillis") {
      value = (value / 1000).toFixed(3);
    } else if(key === "dChiSq") {
      value = value.toFixed(3);
    } else if(key === "lockStatus") {
      if(value < lockStates.length) {
        value = lockStates[value];
      } else {
        value = `??? ${value}`;
      }
    }
    $("#"+key).text(value);
  });

  const time = new Date((json.gpstime-2208988800)*1000);

  offsetData.addRow([time, json.offsetHuman * 1000000000]);
  if(offsetData.getNumberOfRows() > 100) {
    offsetData.removeRow(0);
  }
  offsetChart.draw(offsetData, offsetOptions);

  pidData.addRow([time, json.pidD * 1000000, json.clockPpb/1000]);
  if(pidData.getNumberOfRows() > 100) {
    pidData.removeRow(0);
  }
  pidChart.draw(pidData, pidOptions);

  satelliteData.addRow([time, json.strongSignals, json.weakSignals]);
  if(satelliteData.getNumberOfRows() > 100) {
    satelliteData.removeRow(0);
  }
  satelliteChart.draw(satelliteData, satelliteOptions);

  const gps_radar = $('#gps_radar')[0];
  show_radar(gps_radar);
  show_radar_sats(gps_radar,json.satellites);

  setTimeout(loadClock, 3000);
}

function loadClock() {
  $.getJSON("state.json", gotData);
}
)JS";
