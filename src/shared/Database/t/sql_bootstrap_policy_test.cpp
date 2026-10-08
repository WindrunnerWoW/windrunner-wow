#include "Database/SqlBootstrapPolicy.h"
#include <iostream>
#include <stdexcept>

static void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main()
{
    try
    {
        const std::vector<std::string> dump = {
            "DROP TABLE IF EXISTS `bot_names`;",
            "CREATE TABLE `bot_names` (id INT);",
            "INSERT INTO `bot_names` VALUES (1);",
            "DROP TABLE IF EXISTS `bot_cache`;",
            "CREATE TABLE IF NOT EXISTS `bot_cache` (id INT);",
            "INSERT INTO `bot_cache` VALUES (2);"
        };
        DBUpdater::SqlBootstrapPolicy fresh(dump);
        Check(fresh.tables.size() == 2, "must discover every table in a multi-table dump");
        for (const auto& query : dump)
            Check(!fresh.ShouldSkip(query), "fresh installation must create and seed all tables");

        DBUpdater::SqlBootstrapPolicy partial(dump);
        partial.existingTables.insert("bot_names");
        for (size_t i = 0; i < 3; ++i)
            Check(partial.ShouldSkip(dump[i]), "partial installation must preserve existing names");
        for (size_t i = 3; i < dump.size(); ++i)
            Check(!partial.ShouldSkip(dump[i]), "partial installation must initialize the missing cache");
        Check(partial.ShouldSkip(" DELETE FROM `bot_names`;"), "must preserve existing table contents");
        Check(partial.ShouldSkip("UPDATE bot_names SET id=3;"), "must preserve existing updates");
        Check(partial.ShouldSkip("TRUNCATE TABLE bot_names;"), "must preserve existing truncations");
        Check(partial.ShouldSkip("INSERT IGNORE INTO bot_names VALUES (3);"), "must preserve existing seed rows");
        Check(!partial.ShouldSkip("INSERT INTO bot_names_extra VALUES (3);"), "table matching must not use prefixes");

        DBUpdater::SqlBootstrapPolicy installed(dump);
        installed.existingTables = installed.tables;
        for (const auto& query : dump)
            Check(installed.ShouldSkip(query), "existing installation must adopt the dump without resetting it");
        Check(installed.ShouldSkip("DELETE FROM gossip_menu_option WHERE option_id=99;"),
            "adopting a completed bootstrap must skip its auxiliary side effects");

        DBUpdater::SqlBootstrapPolicy schemaOnly(dump);
        schemaOnly.existingTables = schemaOnly.tables;
        schemaOnly.emptySeedTables.insert("bot_names");
        Check(schemaOnly.ShouldSkip(dump[0]), "schema-only installation must preserve its table");
        Check(schemaOnly.ShouldSkip(dump[1]), "schema-only installation must not recreate its table");
        Check(!schemaOnly.ShouldSkip(dump[2]), "schema-only installation must receive its missing seed data");
        Check(schemaOnly.ShouldSkip(dump[5]), "populated seed tables must remain unchanged");

        DBUpdater::SqlBootstrapPolicy indexes({"create index idx_loot on creature_loot_template(item);"});
        Check(indexes.tables.empty(), "index bootstrap must not pretend to create a base-world table");
        std::string table, index;
        Check(DBUpdater::SqlBootstrapPolicy::CreatedIndex("create index idx_loot on creature_loot_template(item);", table, index),
            "must recognize existing unquoted index scripts");
        Check(table == "creature_loot_template" && index == "idx_loot", "index metadata lookup must target the right table");
        Check(DBUpdater::SqlBootstrapPolicy::CreatedIndex(" CREATE UNIQUE INDEX `idx_loot` ON `loot`(item);", table, index),
            "must support quoted unique indexes");
        Check(!DBUpdater::SqlBootstrapPolicy::CreatedIndex("INSERT INTO loot VALUES (1);", table, index),
            "seed data must not be treated as an index");
        std::cout << "SQL bootstrap policy checks passed.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
