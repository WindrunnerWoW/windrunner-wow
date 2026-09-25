-- ==============================================
-- FILE: guild_bank_money_unsigned.sql
-- GENERATED: 20260731160000
-- ==============================================
-- guild_bank_money.money holds copper and is read into a uint32
-- (GuildBank::b_money), but the column is int(11) - signed. Anything above
-- 2147483647 copper, which is 214748 gold, is written as a negative number and
-- comes back as nonsense on the next load. The %u in the UPDATE hides it.
--
-- Making the column unsigned matches the type the code actually uses and
-- doubles the ceiling to 429496 gold.
--
-- Existing negative values would be a bank that has already overflowed; there
-- is no way to recover the true figure from them, so they are left alone and
-- the conversion will report them.

ALTER TABLE `guild_bank_money`
    MODIFY `money` INT(10) UNSIGNED NOT NULL DEFAULT 0;
