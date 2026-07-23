"""NMEA sentence construction for the GPS MITM test rig.

Builds syntactically valid, checksum-correct NMEA sentences to feed into
teensy-ntp's GPS_SERIAL input in place of a real GPS module -- see
TODO.md, "NMEA MITM tooling", and TESTPLAN.md sections 6/7.

Field layouts and which fields GPS.cpp actually reads are taken directly
from GPS.cpp's decodeTimeCode()/decodeType() comments and switch statements:
- ZDA: time (field 1), day/month/year (fields 2-4). Fields 5-6 (local zone)
  are parsed off the wire but never read.
- RMC: time (field 1), date (field 9, DDMMYY). Fields 2-8/10-12 are never
  read but must still be present so the comma count lines field 9 up
  correctly.
- GGA: none of its fields are read at all -- GPS.cpp uses GGA only as an
  early trigger to snapshot the PPS/local-time reference for a fix cycle,
  before the checksum is even known. Included here only for realism when
  the module in use emits RMC+GGA instead of ZDA.
"""

from datetime import datetime


def checksum(body: str) -> str:
    """XOR of every byte in `body` -- the sentence text with no leading '$'
    and no trailing '*CS', matching GPS.cpp's decode() (parity_ is XORed
    with every character from just after '$' up to just before '*',
    commas included). Same algorithm as GPS.cpp, just run forwards to
    build a sentence instead of backwards to verify one."""
    cs = 0
    for byte in body.encode("ascii"):
        cs ^= byte
    return f"{cs:02X}"


def _sentence(talker_and_type: str, fields: list) -> str:
    body = talker_and_type + "," + ",".join(fields)
    return f"${body}*{checksum(body)}"


def zda(dt: datetime, talker: str = "GP") -> str:
    """$GPZDA,HHMMSS.ss,DD,MM,YYYY,00,00*CS"""
    time_field = dt.strftime("%H%M%S") + ".00"
    return _sentence(talker + "ZDA", [
        time_field, f"{dt.day:02d}", f"{dt.month:02d}", f"{dt.year:04d}", "00", "00",
    ])


def rmc(dt: datetime, talker: str = "GP") -> str:
    """$GPRMC,HHMMSS.ss,A,lat,N,lon,W,speed,course,DDMMYY,0.0,E,A*CS"""
    time_field = dt.strftime("%H%M%S") + ".00"
    date_field = dt.strftime("%d%m%y")
    return _sentence(talker + "RMC", [
        time_field, "A", "5107.0017737", "N", "11402.3291611", "W",
        "0.080", "323.3", date_field, "0.0", "E", "A",
    ])


def gga(dt: datetime, talker: str = "GP") -> str:
    """$GPGGA,HHMMSS.ss,lat,N,lon,W,fix,numsats,hdop,alt,M,sep,M,,*CS"""
    time_field = dt.strftime("%H%M%S") + ".00"
    return _sentence(talker + "GGA", [
        time_field, "5107.0017737", "N", "11402.3291611", "W",
        "1", "08", "0.9", "545.4", "M", "46.9", "M", "", "",
    ])


def fix_group(dt: datetime, sentence_type: str = "zda") -> list:
    """The sentence(s) GPS.cpp needs to see for one fix cycle reporting
    `dt`. `sentence_type` should match whichever this GPS module actually
    emits -- GPS.cpp's own detection is runtime-based (see decodeType()),
    not a compile-time choice, so the fixture needs to match it too."""
    if sentence_type == "zda":
        return [zda(dt)]
    elif sentence_type == "rmc":
        return [rmc(dt), gga(dt)]
    else:
        raise ValueError(f"unknown sentence_type {sentence_type!r}")
