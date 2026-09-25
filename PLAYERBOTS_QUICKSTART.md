# Playerbots quick-start

This branch adds AI-driven player bots (a cmangos/playerbots-style port) on top of
Turtle-WoW's 1.18.1 core, plus a curated set of fixes for bot AI, random-bot
population, and AhBot. It assumes no access to any private/internal build
scripts — everything below uses only what's checked into this repo.

## 1. Prerequisites (Ubuntu 22.04/24.04)

```
sudo apt-get install -y \
  build-essential cmake ninja-build git \
  libace-dev default-libmysqlclient-dev libssl-dev zlib1g-dev \
  libboost-thread-dev libboost-filesystem-dev libboost-system-dev \
  mariadb-server   # or point at any MySQL/MariaDB 10.6+ server
```

You'll also need the Turtle-WoW 1.18.1 (build 7272) client's extracted map/vmap/dbc
data (`maps`, `vmaps`, `mmaps`, `dbc` folders) somewhere on disk — this repo doesn't
ship client-derived data. Extraction tools live under `tools/` in a full MaNGOS-family
checkout; consult a MaNGOS/CMaNGOS extraction guide if you don't already have this data.

## 2. Build

```
git clone <this-repo-url> tortoise-wow
cd tortoise-wow
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_PLAYERBOTS=ON -DALLOW_TURTLE_ADDONS=ON
cmake --build build --target mangosd -j$(nproc)
```

> Login without `-DALLOW_TURTLE_ADDONS=ON` can throw a client-side login error —
> a known issue, so build with that flag on.

`BUILD_PLAYERBOTS=ON` is required to compile the bot module at all; it's `OFF` by
default so the core still builds standalone without it.

## 3. Database setup

Point these at a MySQL/MariaDB 10.6+ server. Adjust host/user/password as needed —
examples below assume a local server with user `mangos`/password `mangos`:

```
mysql -u mangos -p < sql/create_databases.sql        # creates tw_logon/tw_char/tw_world/tw_logs schemas
for f in sql/base/*.sql; do mysql -u mangos -p tw_world < "$f"; done

# Playerbot-specific tables (not part of the base dump above):
for f in src/modules/PlayerBots/sql/world/ai_playerbot_indexes.sql \
         src/modules/PlayerBots/sql/world/ai_playerbot_rpg_races.sql \
         src/modules/PlayerBots/sql/world/ai_playerbot_texts.sql \
         src/modules/PlayerBots/sql/world/classic/*.sql; do
  mysql -u mangos -p tw_world < "$f"
done
for f in src/modules/PlayerBots/sql/characters/*.sql; do
  mysql -u mangos -p tw_char < "$f"
done
```

`Database.AutoUpdate.Enabled = 1` in `mangosd.conf` (see below) applies anything
under `sql/database_updates/` automatically on subsequent starts — the steps above
are only needed once, for a brand-new database.

## 4. Configure

```
cp build/src/mangosd/mangosd.conf.dist build/src/mangosd/mangosd.conf
cp src/modules/PlayerBots/playerbot/aiplayerbot.conf.dist.in <SYSCONFDIR>/aiplayerbot.conf
```

`<SYSCONFDIR>` is the path baked in at configure time (defaults to
`<install-prefix>/etc/`; check the `SYSCONFDIR` compile define, or just pass
`-DCMAKE_INSTALL_PREFIX=...` and use `<prefix>/etc/`). `aiplayerbot.conf` is looked
up there regardless of where `mangosd.conf` lives.

Edit `mangosd.conf`:
- `DataDir` → your extracted maps/vmaps/mmaps/dbc directory
- `LoginDatabase.Info` / `WorldDatabase.Info` / `CharacterDatabase.Info` /
  `LogsDatabase.Info` → `"host;port;user;password;dbname"` (tw_logon / tw_world /
  tw_char / tw_logs respectively)
- `Database.AutoUpdate.Path` → absolute path to this repo's `sql/` directory (a
  relative path is resolved against the server's current working directory, not
  the binary's location, so an absolute path is safer)

Edit `aiplayerbot.conf` — key settings to get a first cohort running:
- `AiPlayerbot.Enabled = 1`
- `AiPlayerbot.RandomBotAutoCreate = 1` (default) — on any boot where fewer than
  `MinRandomBots` exist, it creates more automatically. This is all you need for
  a brand-new database; no special flag is required for the very first start.
- `AiPlayerbot.MinRandomBots` / `MaxRandomBots` — start small (e.g. `20`) for a
  first run; the bots' equipment/talent cache is (re)built from the world DB on
  every boot where new random bots need creating, which takes longer at higher
  counts.
- `AiPlayerbot.DeleteRandomBotAccounts` — leave at `0` normally. Only set it to
  `1` for exactly one server start when you want to wipe and fully recreate an
  *existing* cohort from scratch (e.g. after changing bot count or class/race
  probabilities) — **set it back to `0` after that reset start**, or it will
  wipe and recreate the cohort on every subsequent restart.

## 5. Run

```
cd build/src/mangosd
./mangosd -c mangosd.conf
```

On a fresh database, first boot will build the playerbot item/talent caches (can
take a few minutes depending on cohort size) and then create the random-bot
accounts/characters. Look for `World server is up and running!` in the log with
no `Assertion`/`terminate called` errors, and check `character` counts in the
`tw_char` database match `MinRandomBots`/`MaxRandomBots` to confirm the cohort
was created successfully.

You'll also need `realmd` (built the same way, target `realmd`) and a matching
`realmd.conf` pointing at `tw_logon` if you want real players to log in — the
playerbot cohort itself doesn't require realmd to be running.
