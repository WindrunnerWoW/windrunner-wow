"""Ermittelt, welche Migrationen bereits in sql/base stecken.

Zweiter Anlauf. Der erste teilte den Dateiinhalt an ';' in Bloecke und ordnete
jedem Block die erste darin gefundene Tabelle zu - bei Dateien, die mehrere
Tabellen in einem Block haben, landeten damit alle Werte bei der falschen
Tabelle. Jetzt wird stattdessen ab jedem INSERT bis zum naechsten INSERT
gelesen, sodass Tabelle und Werte immer zusammengehoeren.

Grenzen bleiben: verglichen wird der erste Spaltenwert je Zeile, bei
zusammengesetzten Schluesseln ist das eine Naeherung. Und eine Migration, deren
Zeilen spaeter wieder geloescht wurden, sieht aus wie "nicht angewendet".
"""
import io
import os
import re
from collections import OrderedDict

BASIS = '/home/shyalya/tortoise-playerbots/sql/'
BASE = BASIS + 'base/'
UPD = BASIS + 'database_updates/'

KOPF = re.compile(r'\b(?:INSERT|REPLACE)\s+(?:IGNORE\s+)?INTO\s+`?(\w+)`?', re.I)
ERSTER = re.compile(r"\(\s*(-?\d+)\s*,")

cache = {}


def basis_schluessel(tabelle):
    if tabelle in cache:
        return cache[tabelle]
    pfad = BASE + 'tw_world_%s.sql' % tabelle
    if not os.path.exists(pfad):
        cache[tabelle] = None
        return None
    werte = set()
    with io.open(pfad, encoding='utf8', errors='replace') as f:
        for zeile in f:
            if KOPF.search(zeile):
                werte.update(m.group(1) for m in ERSTER.finditer(zeile))
    cache[tabelle] = werte
    return werte


def untersuche(pfad):
    t = io.open(pfad, encoding='utf8', errors='replace').read()
    hat_alter = bool(re.search(r'\bALTER\s+TABLE\b', t, re.I))

    treffer = list(KOPF.finditer(t))
    drin = neu = 0
    ohne_basis = OrderedDict()

    for i, m in enumerate(treffer):
        tab = m.group(1)
        ende = treffer[i + 1].start() if i + 1 < len(treffer) else len(t)
        abschnitt = t[m.end():ende]

        basis = basis_schluessel(tab)
        if basis is None:
            ohne_basis[tab] = True
            continue

        for w in ERSTER.finditer(abschnitt):
            if w.group(1) in basis:
                drin += 1
            else:
                neu += 1

    return hat_alter, drin, neu, list(ohne_basis)


dateien = sorted(f for f in os.listdir(UPD) if f.endswith('.sql'))
print('%d Migrationen gegen sql/base geprueft\n' % len(dateien))
print('%-28s %8s %8s %6s  %s' % ('Migration', 'drin', 'neu', 'ALTER', 'Urteil'))
print('-' * 84)

drin_liste, offen_liste, unklar_liste = [], [], []

for name in dateien:
    hat_alter, drin, neu, fehlende_tab = untersuche(UPD + name)
    gesamt = drin + neu

    if gesamt == 0:
        urteil = 'keine Schluessel'
        if fehlende_tab:
            urteil += ' (Tabelle nicht in base: %s)' % ', '.join(fehlende_tab[:2])
        unklar_liste.append(name)
    elif drin / gesamt >= 0.95:
        urteil = 'steckt in der Momentaufnahme'
        drin_liste.append(name)
    elif drin / gesamt <= 0.05:
        urteil = 'noch nicht angewendet'
        offen_liste.append(name)
    else:
        urteil = 'teilweise %.0f%%' % (100.0 * drin / gesamt)
        unklar_liste.append(name)

    print('%-28s %8d %8d %6s  %s' % (name, drin, neu, 'ja' if hat_alter else '-', urteil))

print('\n' + '=' * 84)
print('steckt in der Momentaufnahme: %3d' % len(drin_liste))
print('noch nicht angewendet:        %3d' % len(offen_liste))
print('unklar:                       %3d' % len(unklar_liste))
