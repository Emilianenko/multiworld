function onStartup()


	local time = os.time()
	db.asyncQuery('DELETE FROM `players_online` WHERE `world_id` = '..configManager.getNumber(configKeys.WORLD_ID)..'')

	-- zerar storages e permitir compra de boost na store
	db.query('UPDATE `player_storage` SET `value` = 0 WHERE `player_storage`.`key` = 51052')

	-- deletar as guilds canceladas e rejeitadas
	db.asyncQuery('DELETE FROM `guild_wars` WHERE `status` = 2')
	db.asyncQuery('DELETE FROM `guild_wars` WHERE `status` = 3')

	-- deletar as guilds que estão muito tempo pendentes 3 dias
	db.asyncQuery('DELETE FROM `guild_wars` WHERE `status` = 0 AND (`started` + 72 * 60 * 60) <= ' .. os.time())

	--db.asyncQuery("UPDATE `guild_wars` SET `status` = 4, `ended` = " .. os.time() .. " WHERE `status` = 1 AND (`started` + 3* 60 * 60) < " .. os.time())
	db.asyncQuery('DELETE FROM `players` WHERE `deletion` != 0 AND `deletion` < ' .. time)
	db.asyncQuery('DELETE FROM `ip_bans` WHERE `expires_at` != 0 AND `expires_at` <= ' .. time)
	db.asyncQuery('DELETE FROM `market_history` WHERE `inserted` <= ' .. (time - configManager.getNumber(configKeys.MARKET_OFFER_DURATION)))


	-- Move expired bans to ban history
	local resultId = db.storeQuery("SELECT * FROM `account_bans` WHERE `expires_at` != 0 AND `expires_at` <= " .. os.time())
	if resultId ~= false then
		repeat
			local accountId = result.getNumber(resultId, "account_id")
			db.asyncQuery("INSERT INTO `account_ban_history` (`account_id`, `reason`, `banned_at`, `expired_at`, `banned_by`) VALUES (" .. accountId .. ", " .. db.escapeString(result.getString(resultId, "reason")) .. ", " .. result.getNumber(resultId, "banned_at") .. ", " .. result.getNumber(resultId, "expires_at") .. ", " .. result.getNumber(resultId, "banned_by") .. ")")
			db.asyncQuery("DELETE FROM `account_bans` WHERE `account_id` = " .. accountId)
		until not result.next(resultId)
		result.free(resultId)
	end

	-- Check house auctions

	local resultId = db.storeQuery('SELECT `id`, `world_id`, `highest_bidder`, `last_bid`, (SELECT `balance` FROM `players` WHERE `players`.`id` = `highest_bidder`) AS `balance` FROM `houses` WHERE `owner` = 0 AND `world_id` = '..configManager.getNumber(configKeys.WORLD_ID)..' AND `bid_end` != 0 AND `bid_end` < ' .. time)

	if resultId ~= false then
		repeat
			local house = House(result.getNumber(resultId, "id"))
			if house then
				local highestBidder = result.getNumber(resultId, "highest_bidder")
				local balance = result.getNumber(resultId, "balance")
				local lastBid = result.getNumber(resultId, "last_bid")
				if balance >= lastBid then
					db.query("UPDATE `players` SET `balance` = " .. (balance - lastBid) .. " WHERE `id` = " .. highestBidder)
					house:setOwnerGuid(highestBidder)
				end

				db.asyncQuery('UPDATE `houses` SET `last_bid` = 0, `bid_end` = 0, `highest_bidder` = 0, `bid` = 0 WHERE `world_id` = '..configManager.getNumber(configKeys.WORLD_ID)..' and `id` = ' .. house:getId())

			end
		until not result.next(resultId)
		result.free(resultId)
	end

	-- store towns in database
	db.query("TRUNCATE TABLE `towns`")
	for i, town in ipairs(Game.getTowns()) do
		local position = town:getTemplePosition()
		db.query("INSERT INTO `towns` (`id`, `name`, `posx`, `posy`, `posz`) VALUES (" .. town:getId() .. ", " .. db.escapeString(town:getName()) .. ", " .. position.x .. ", " .. position.y .. ", " .. position.z .. ")")
	end
end
