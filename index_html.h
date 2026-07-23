static const PROGMEM char indexHTML[] = R"HTML(<html><head>
<script src="https://code.jquery.com/jquery-3.5.1.min.js" integrity="sha256-9/aliU8dGd2tb6OSsuzixeV4y/faTqgFtohetphbbj0=" crossorigin="anonymous"></script>
<script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script>
<script src="index.js"></script>
<title>NTP/GPS state</title>
<style>
h3 { margin-bottom: 0.3em; }
table { border-collapse: collapse; margin: 0 0 1em 0; }
th, td { text-align: left; padding: 1px 8px 1px 0; vertical-align: top; }
th { font-weight: normal; color: #555; white-space: nowrap; }
</style>
</head><body>
<h2>Teensy 4.1 NTP server</h2>
<div id='offsetChart' style='height: 400px; width: 100%'></div>
<div id='pidChart' style='height: 400px; width: 100%'></div>
<div id='satelliteChart' style='height: 400px; width: 100%'></div>
<div id=gps>
<canvas style="display: block" id="gps_radar" height=315 width=355></canvas>
</div>

<h3>Clock</h3>
<table>
<tr><th>NTP time</th><td><span id='gpstime'></span> (<span id='gpstimeHuman'></span>)</td></tr>
<tr><th>GPS reported time</th><td><span id='gpsReportedTime'></span> (<span id='gpsReportedTimeHuman'></span>)</td></tr>
<tr><th>Offset between NTP/GPS times</th><td><span id='offsetHuman'></span> s</td></tr>
<tr><th>IEEE 1588 counter at PPS</th><td><span id='counterPPS'></span></td></tr>
</table>

<h3>Clock discipline (PID)</h3>
<table>
<tr><th>Estimate of NTP clock freq</th><td><span id='pidD'></span> s/s</td></tr>
<tr><th>ChiSq fit of freq measure</th><td><span id='dChiSq'></span></td></tr>
<tr><th>PID output</th><td><span id='clockPpb'></span> ns/s (ppb)</td></tr>
</table>

<h3>Holdover</h3>
<table>
<tr><th>In holdover (GPS/PPS lost)</th><td><span id='inHoldover'></span></td></tr>
<tr><th>Holdover dispersion estimate</th><td><span id='holdoverDispersion'></span> s</td></tr>
<tr><th>Holdover elapsed</th><td><span id='holdoverElapsedMs'></span> s</td></tr>
</table>

<h3>PPS/GPS timing diagnostics</h3>
<table>
<tr><th>PPS To GPS</th><td><span id='ppsToGPS'></span> ms</td></tr>
<tr><th>millis() at PPS</th><td><span id='ppsMillis'></span></td></tr>
<tr><th>millis() at GPS Timestamp</th><td><span id='gpsCaptured'></span></td></tr>
<tr><th>millis() now</th><td><span id='curMillis'></span></td></tr>
</table>

<h3>GPS reception</h3>
<table>
<tr><th>GPS lock status</th><td><span id='lockStatus'></span></td></tr>
<tr><th>Strong signals (&gt;25dB)</th><td><span id='strongSignals'></span></td></tr>
<tr><th>Weak signals (10-24dB)</th><td><span id='weakSignals'></span></td></tr>
<tr><th>No signal (0-9dB)</th><td><span id='noSignals'></span></td></tr>
<tr><th>DOP</th><td>pdop=<span id='pdop'></span>, hdop=<span id='hdop'></span>, vdop=<span id='vdop'></span></td></tr>
</table>
</body></html>
)HTML";
