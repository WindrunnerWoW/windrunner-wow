-- Remove the recurring unofficial-continuation disclaimer from chat.
DELETE FROM `autobroadcast` WHERE `string_id` = 50009;
