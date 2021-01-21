static const PROGMEM char indexHTML[] = R"HTML(<html><head>
<script src="https://code.jquery.com/jquery-3.5.1.min.js" integrity="sha256-9/aliU8dGd2tb6OSsuzixeV4y/faTqgFtohetphbbj0=" crossorigin="anonymous"></script>
<script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script>
<script src="index.js"></script>
<title>NTP/GPS state</title>
</head><body>
<h2>Teensy 4.1 NTP server</h2>
<div id='offsetChart' style='height: 400px; width: 100%'></div>
<div id='pidChart' style='height: 400px; width: 100%'></div>
<div id='satelliteChart' style='height: 400px; width: 100%'></div>
<div id=gps>
<canvas style="display: block" id="gps_radar" height=315 width=355></canvas>
</div>
<p>PPS To GPS: <span id='ppsToGPS'></span> ms</p>
<p>millis() at PPS: <span id='ppsMillis'></span></p>
<p>millis() at GPS Timestamp: <span id='gpsCaptured'></span></p>
<p>millis() now: <span id='curMillis'></span></p>
<p>NTP time: <span id='gpstime'></span></p>
<p>IEEE 1588 counter at PPS: <span id='counterPPS'></span></p>
<p>Offset between NTP/GPS times: <span id='offsetHuman'></span> s</p>
<p>Estimate of NTP clock freq: <span id='pidD'></span> s/s</p>
<p>ChiSq fit of freq measure: <span id='dChiSq'></span></p>
<p>PID output: <span id='clockPpb'></span> ns/s (ppb)</p>
<p>GPS lock Status: <span id='lockStatus'></span></p>
<p>GPS Strong signals (&gt; 25db): <span id='strongSignals'></span>, Weak Signals (10db-24db): <span id='weakSignals'></span>, No Signal (0db-9db): <span id='noSignals'></span></p>
</body></html>
)HTML";
