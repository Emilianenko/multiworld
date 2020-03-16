/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2019  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "otpch.h"

#include "databasemanager.h"

#include "configmanager.h"
#include "luascript.h"
#include "gameserverconfig.h"

extern ConfigManager g_config;
extern GameserverConfig g_gameserver;

bool DatabaseManager::optimizeTables()
{
	Database& db = Database::getInstance();

	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `TABLE_NAME` FROM `information_schema`.`TABLES` WHERE `TABLE_SCHEMA` = {:s} AND `DATA_FREE` > 0", db.escapeString(g_config.getString(ConfigManager::MYSQL_DB))));
	if (!result) {
		return false;
	}

	do {
		std::string tableName = result->getString("TABLE_NAME");
		std::cout << "> Optimizing table " << tableName << "..." << std::flush;

		if (db.executeQuery(fmt::format("OPTIMIZE TABLE `{:s}`", tableName))) {
			std::cout << " [success]" << std::endl;
		} else {
			std::cout << " [failed]" << std::endl;
		}
	} while (result->next());
	return true;
}

bool DatabaseManager::tableExists(const std::string& tableName)
{
	Database& db = Database::getInstance();
	return db.storeQuery(fmt::format("SELECT `TABLE_NAME` FROM `information_schema`.`tables` WHERE `TABLE_SCHEMA` = {:s} AND `TABLE_NAME` = {:s} LIMIT 1", db.escapeString(g_config.getString(ConfigManager::MYSQL_DB)), db.escapeString(tableName))).get() != nullptr;
}

bool DatabaseManager::isDatabaseSetup()
{
	Database& db = Database::getInstance();
	return db.storeQuery(fmt::format("SELECT `TABLE_NAME` FROM `information_schema`.`tables` WHERE `TABLE_SCHEMA` = {:s}", db.escapeString(g_config.getString(ConfigManager::MYSQL_DB)))).get() != nullptr;
}

int32_t DatabaseManager::getDatabaseVersion()
{
	uint16_t worldId = g_gameserver.getWorldId();
	
	if (!tableExists("server_config")) {
		Database& db = Database::getInstance();
		std::ostringstream createQuery;
		createQuery << "CREATE TABLE `server_config` (`world_id` INT(11) NOT NULL DEFAULT 0, `config` VARCHAR(50) NOT NULL, `value` VARCHAR(256) NOT NULL DEFAULT '') ENGINE = InnoDB";
		db.executeQuery(createQuery.str());

		std::ostringstream insertQuery;
		insertQuery << "INSERT INTO `server_config` VALUES ("<< worldId <<", 'db_version', 0)";
		db.executeQuery(insertQuery.str());
		return 0;
	}

	int32_t version = 0;
	if (getDatabaseConfig("db_version", version)) {
		return version;
	}
	return -1;
}

void DatabaseManager::updateDatabase()
{
	lua_State* L = luaL_newstate();
	if (!L) {
		return;
	}

	luaL_openlibs(L);

#ifndef LUAJIT_VERSION
	//bit operations for Lua, based on bitlib project release 24
	//bit.bnot, bit.band, bit.bor, bit.bxor, bit.lshift, bit.rshift
	luaL_register(L, "bit", LuaScriptInterface::luaBitReg);
#endif

	//db table
	luaL_register(L, "db", LuaScriptInterface::luaDatabaseTable);

	//result table
	luaL_register(L, "result", LuaScriptInterface::luaResultTable);

	int32_t version = getDatabaseVersion();
	do {
		if (luaL_dofile(L, fmt::format("data/migrations/{:d}.lua", version).c_str()) != 0) {
			std::cout << "[Error - DatabaseManager::updateDatabase - Version: " << version << "] " << lua_tostring(L, -1) << std::endl;
			break;
		}

		if (!LuaScriptInterface::reserveScriptEnv()) {
			break;
		}

		lua_getglobal(L, "onUpdateDatabase");
		if (lua_pcall(L, 0, 1, 0) != 0) {
			LuaScriptInterface::resetScriptEnv();
			std::cout << "[Error - DatabaseManager::updateDatabase - Version: " << version << "] " << lua_tostring(L, -1) << std::endl;
			break;
		}

		if (!LuaScriptInterface::getBoolean(L, -1, false)) {
			LuaScriptInterface::resetScriptEnv();
			break;
		}

		version++;
		std::cout << "> Database has been updated to version " << version << '.' << std::endl;
		registerDatabaseConfig("db_version", version);

		LuaScriptInterface::resetScriptEnv();
	} while (true);
	lua_close(L);
}

bool DatabaseManager::getDatabaseConfig(const std::string& config, int32_t& value)
{
	Database& db = Database::getInstance();
	std::ostringstream query;
	uint16_t worldId = g_gameserver.getWorldId();

	query << "SELECT `value` FROM `server_config` WHERE `world_id` = "<< worldId <<" AND `config` = " << db.escapeString(config);


	DBResult_ptr result = db.storeQuery(fmt::format("SELECT `value` FROM `server_config` WHERE `config` = {:s}", db.escapeString(config)));
	if (!result) {
		return false;
	}

	value = result->getNumber<int32_t>("value");
	return true;
}

void DatabaseManager::registerDatabaseConfig(const std::string& config, int32_t value)
{
	Database& db = Database::getInstance();

	std::ostringstream query;
	uint16_t worldId = g_gameserver.getWorldId();


	int32_t tmp;

	if (!getDatabaseConfig(config, tmp)) {

		query << "INSERT INTO `server_config` VALUES (" << worldId << ", " << db.escapeString(config) << ", '" << value << "')";
	} else {
		query << "UPDATE `server_config` SET `value` = '" << value << "' WHERE `world_id` = " << worldId << " AND `config` = " << db.escapeString(config);

	}
}
