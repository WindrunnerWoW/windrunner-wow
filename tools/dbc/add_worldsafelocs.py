"""Ergaenzt fehlende Friedhoefe in WorldSafeLocs.dbc.

Die Weltdatenbank verweist in game_graveyard_zone auf die Friedhoefe 934, 937
und 950 fuer die Turtle-Hochelfenzonen Quel'Thalas, Amani'Alor und Alah'Thalas.
Unsere DBC kennt nur die IDs 1-174, weshalb der Server beim Start meldet
"has record for not existing graveyard ... skipped" - diese Zonen haben dadurch
gar keinen Friedhof, und wer dort stirbt, erscheint als Geist an der eigenen
Leiche statt an einem Friedhof.

Auch die Client-DBC aus patch-9 hat nur 174 Eintraege; die zusaetzlichen
Friedhoefe existierten nur serverseitig bei Turtle. Die Positionen lassen sich
aber exakt aus game_tele rekonstruieren ('alahthalasgraveyard',
'amanialorgraveyard').

Aufbau der Datei: Header (20 Byte), dann rec Datensaetze zu je 56 Byte
(14 Felder), dann der Stringblock. Beim Anhaengen verschiebt sich der
Stringblock - die gespeicherten Offsets bleiben aber gueltig, weil sie relativ
zum Blockanfang sind und die Reihenfolge erhalten bleibt.
"""
import struct
import sys

QUELLE = sys.argv[1] if len(sys.argv) > 1 else 'WorldSafeLocs.dbc'
ZIEL = sys.argv[2] if len(sys.argv) > 2 else 'WorldSafeLocs_neu.dbc'

# id, map, x, y, z, Name
NEU = [
    # High elf zones - coordinates recovered from game_tele
    # ('alahthalasgraveyard', 'amanialorgraveyard')
    (934, 0, 4284.21, -2862.30, 5.13, "Quel'Thalas"),
    (937, 1, 2949.87, 2557.54, 139.18, "Amani'Alor"),
    (950, 0, 4284.21, -2862.30, 5.13, "Alah'Thalas"),
    # Turtle-built dungeons with no graveyard anywhere on their map.
    # Coordinates are each instance's entrance from areatrigger_teleport,
    # i.e. just inside the door. Pair these with sql/graveyards_dungeons.sql.
    (960, 532, -11104.0, -1999.0, 50.0, "Lower Karazhan Halls"),
    (961, 816, -6105.0, -3630.0, 242.0, "Dragonmaw Retreat"),
    (962, 819, -8134.0, -3450.0, 225.0, "Timbermaw Hold"),
    (963, 820, -8026.0, -3510.0, 158.0, "Windhorn Canyon"),
    (964, 822, -7523.0, -3589.0, 200.0, "Frostmane Hollow"),
]

roh = open(QUELLE, 'rb').read()
magie, rec, fld, rsz, ssz = struct.unpack('<4sIIII', roh[:20])
assert magie == b'WDBC', "keine DBC-Datei"
kopf_ende = 20
saetze = roh[kopf_ende:kopf_ende + rec * rsz]
strings = roh[kopf_ende + rec * rsz:]
assert len(strings) == ssz, "Stringblock passt nicht zum Header"

vorhandene = {struct.unpack('<I', saetze[i * rsz:i * rsz + 4])[0] for i in range(rec)}
neue = [n for n in NEU if n[0] not in vorhandene]
if not neue:
    print("Nichts zu tun - alle IDs bereits vorhanden.")
    sys.exit(0)

zusatz_saetze = b''
zusatz_strings = b''
for wid, mapid, x, y, z, name in neue:
    offset = ssz + len(zusatz_strings)
    zusatz_strings += name.encode('utf8') + b'\0'
    felder = [wid, mapid]
    satz = struct.pack('<2I', *felder) + struct.pack('<3f', x, y, z)
    satz += struct.pack('<I', offset)          # Name, Locale 0 (englisch)
    satz += struct.pack('<7I', *([0] * 7))     # weitere Locales leer
    satz += struct.pack('<I', 0)               # String-Flags
    assert len(satz) == rsz, "Satzlaenge %d != %d" % (len(satz), rsz)
    zusatz_saetze += satz
    print("  + ID %-4d Karte %d (%.0f, %.0f, %.0f)  %s" % (wid, mapid, x, y, z, name))

kopf = struct.pack('<4sIIII', magie, rec + len(neue), fld, rsz, ssz + len(zusatz_strings))
open(ZIEL, 'wb').write(kopf + saetze + zusatz_saetze + strings + zusatz_strings)
print("Eintraege: %d -> %d" % (rec, rec + len(neue)))
