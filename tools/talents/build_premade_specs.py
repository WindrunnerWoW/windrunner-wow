#!/usr/bin/env python3
"""Generate the AiPlayerbot.PremadeSpec* block for aiplayerbot.conf.

The stock links that ship with the playerbot module are vanilla ones, and every
single one of them is rejected against Turtle's reworked talent trees - the log
says "Error with premade spec link" once per entry and "No premade specs found!!"
at the end, and the bots then run around with no talents at all.

The builds below were put together by hand in game, one per role where a class
has one. This script turns each of them into a full set of config entries.

Two things it takes care of that are easy to get wrong:

  Levelling path. ChangeTalentsAction::GetBestPremadeSpec hands out the first
  entry whose point total reaches the bot's, so with only a level 60 entry even
  a level 20 bot gets that one - and since TalentSpec applies points in tree
  order, it would spend its first points in the secondary tree. A feral druid
  built to tank would start out as a balance caster. Each build therefore gets
  an entry every five levels, filled along a learning order: main tree first by
  row and column, then the rest. Every entry is a prefix of the final build, so
  the row rule holds throughout.

  Talent rate. The points a level grants are (level - 9) * Rate.Talent, so the
  links depend on the rate the server runs. Generated for the wrong one, every
  entry demands a higher level than it is filed under and the server rejects the
  lot. Pass --rate to match your mangosd.conf; the default matches the shipped
  Rate.Talent = 1. At a higher rate the level 60 entries simply carry more
  points, up to the full build.

Usage:
    build_premade_specs.py --dbc /path/to/dbc [--rate 1.0] [--out file]
"""
import argparse
import struct
import sys

LEVELS = [10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60]

# Hand-built level 60 talent distributions, 61 points each.
BUILDS = {
    1:  [('fury',          '352200123025101-05555000003122501'),
         ('protection',    '3500531--2530510232055122231')],
    2:  [('holy',          '05320302035251351-50025-252'),
         ('protection',    '5-0531231332031551-05205001203'),
         ('retribution',   '50005-50325000003-5023005123005151')],
    3:  [('beastmastery',  '5500320152253112251-052521001'),
         ('marksmanship',  '502300015201-0555213250123251')],
    4:  [('combat',        '0053231052330012-500350200050122231'),
         ('assassination', '005323105233311251-500350020050001')],
    5:  [('holy',          '2052022013300301-32050203023521351'),
         ('shadow',        '20504020133003115--05225000102501251')],
    7:  [('restoration',   '50203105-500002-5503032105321251'),
         ('enhancement',   '550331300202012-5005303105023151')],
    8:  [('arcane',        '2352551312233311251-55000001'),
         ('fire',          '230205111200301-50523201030303251-203'),
         ('frost',         '230225100210301--0533020510235110521')],
    9:  [('affliction',    '550022300223210151-053030101-5005301'),
         ('demonology',    '50002-250033150223313531-5025'),
         ('destruction',   '-053030101-5505221522525151')],
    11: [('balance',       '50000230312533113321--5030513300012'),
         ('feral',         '0140003201-553030213232021521-55'),
         ('restoration',   '5002013031203--5050013355213521')],
}


def read_dbc(directory, name):
    raw = open(directory + '/' + name, 'rb').read()
    _, records, _, size, _ = struct.unpack('<4sIIII', raw[:20])
    for i in range(records):
        yield struct.unpack('<%dI' % (size // 4), raw[20 + i * size:20 + (i + 1) * size])


def load_talents(dbc):
    tabs = {v[0]: {'classMask': v[12], 'page': v[13]} for v in read_dbc(dbc, 'TalentTab.dbc')}

    talents = []
    for v in read_dbc(dbc, 'Talent.dbc'):
        tab = tabs.get(v[1])
        if not tab:
            continue
        talents.append({
            'id': v[0], 'row': v[2], 'col': v[3],
            'maxRank': sum(1 for k in range(5) if v[4 + k]),
            'dependsOn': v[13], 'dependsOnRank': v[16],
            'classMask': tab['classMask'],
            # Talent tab 41 is filed under the wrong page in Turtle's DBC.
            'page': 1 if v[1] == 41 else tab['page'],
        })
    return talents


def talents_of(talents, cls):
    """Exactly the order TalentSpec::GetTalents produces, including the
    SortTalents(SORT_BY_DEFAULT) at the end. Turtle's Talent.dbc is not stored
    in tree order; without this sort every rank lands in the wrong tree."""
    mask = 1 << (cls - 1)
    hits = [t for t in talents if t['classMask'] & mask]
    hits.sort(key=lambda t: (t['page'], t['row'], t['col']))
    return hits


def parse_link(link, entries):
    """Map the digits of a talent link onto talents, as ReadTalents does."""
    wanted = {}
    page = pos = 0
    while pos < len(link) and link[pos] == '-':
        pos += 1
        page += 1
    for e in entries:
        if pos >= len(link):
            break
        if e['page'] != page:
            continue
        wanted[e['id']] = int(link[pos])
        pos += 1
        while pos < len(link) and link[pos] == '-':
            pos += 1
            page += 1
    return wanted


def check(entries, ranks, budget):
    """The same four rules as TalentSpec::CheckTalents."""
    by_id = {e['id']: e for e in entries}
    for e in entries:
        r = ranks.get(e['id'], 0)
        if r > e['maxRank']:
            return 'rank above maximum on talent %d' % e['id']
        if r > 0 and e['dependsOn']:
            prev = by_id.get(e['dependsOn'])
            if not prev or ranks.get(prev['id'], 0) < e['dependsOnRank']:
                return 'prerequisite missing for talent %d' % e['id']
    for page in (0, 1, 2):
        spent = 0
        for e in [x for x in entries if x['page'] == page]:
            if ranks.get(e['id'], 0) > 0 and e['row'] * 5 > spent:
                return 'row rule broken on talent %d (row %d, only %d spent)' % (
                    e['id'], e['row'], spent)
            spent += ranks.get(e['id'], 0)
    total = sum(ranks.values())
    if total > budget:
        return 'too many points (%d of %d)' % (total, budget)
    return None


def build_link(entries, ranks):
    parts = []
    for page in (0, 1, 2):
        tree = [e for e in entries if e['page'] == page]
        parts.append(''.join(str(ranks.get(e['id'], 0)) for e in tree).rstrip('0'))
    return '-'.join(parts).rstrip('-')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dbc', required=True, help='directory holding Talent.dbc and TalentTab.dbc')
    ap.add_argument('--rate', type=float, default=1.0, help='Rate.Talent from mangosd.conf')
    ap.add_argument('--out', help='write here instead of stdout')
    args = ap.parse_args()

    talents = load_talents(args.dbc)
    out = []
    failures = 0

    for cls in sorted(BUILDS):
        entries = talents_of(talents, cls)
        out.append('')
        out.append('# --- class %d ---' % cls)

        for index, (name, link) in enumerate(BUILDS[cls]):
            target = parse_link(link, entries)

            # Validate the build itself, not against a level cap: at a low talent
            # rate the full build simply does not fit level 60, and the entries
            # below are truncated to what does.
            problem = check(entries, target, sum(target.values()))
            if problem:
                print('  BROKEN  class %d %s: %s' % (cls, name, problem), file=sys.stderr)
                failures += 1
                continue

            main_tree = max((0, 1, 2), key=lambda p: sum(
                target.get(e['id'], 0) for e in entries if e['page'] == p))

            order = ([e for e in entries if e['page'] == main_tree]
                     + [e for e in entries if e['page'] != main_tree])

            out.append('AiPlayerbot.PremadeSpecName.%d.%d = %s' % (cls, index, name))
            out.append('AiPlayerbot.PremadeSpecProb.%d.%d = 100' % (cls, index))

            for level in LEVELS:
                budget = int((level - 9) * args.rate)
                ranks, spent = {}, 0
                for e in order:
                    if spent >= budget:
                        break
                    add = min(target.get(e['id'], 0), budget - spent)
                    if add > 0:
                        ranks[e['id']] = add
                        spent += add

                problem = check(entries, ranks, budget)
                if problem:
                    print('  BROKEN  class %d %s at level %d: %s'
                          % (cls, name, level, problem), file=sys.stderr)
                    failures += 1
                    continue

                out.append('AiPlayerbot.PremadeSpecLink.%d.%d.%d = %s'
                           % (cls, index, level, build_link(entries, ranks)))

            full = sum(target.values())
            at_sixty = min(int((60 - 9) * args.rate), full)
            print('  class %-2d %-14s main tree %d, %2d of %d points at level 60'
                  % (cls, name, main_tree, at_sixty, full), file=sys.stderr)

    text = '\n'.join(out) + '\n'
    if args.out:
        open(args.out, 'w', newline='\n').write(text)
    else:
        sys.stdout.write(text)

    print('\nfailures: %d' % failures, file=sys.stderr)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
