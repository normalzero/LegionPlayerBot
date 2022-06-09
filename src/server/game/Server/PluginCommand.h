
#ifndef _PLUGIN_COMMAND_H_
#define _PLUGIN_COMMAND_H_

#include "Log.h"
#include "Common.h"
#include "SharedDefines.h"
#include "DatabaseEnv.h"
#include "Player.h"

class PluginCommand
{
private:
	PluginCommand() {};
	~PluginCommand() {};

public:
	PluginCommand(PluginCommand const&) = delete;
	PluginCommand(PluginCommand&&) = delete;

	PluginCommand& operator= (PluginCommand const&) = delete;
	PluginCommand& operator= (PluginCommand&&) = delete;

	static PluginCommand* instance();

	bool ProcessCommand(Player* player, std::string cmd);

private:
	bool BindingHomePosition(Player* player);
	bool SuperMenu(Player* player);
	bool OnlineCmd(Player* player, uint32 cls);
	bool AccountCmd(Player* player, uint32 cls);
	bool OfflineCmd(Player* player);
	bool AddGroupFriend(Player* player);
	bool ResetDungeon(Player* player);
	bool OnlineFriends(Player* player);
	bool Saveall(Player* player);
	bool ToggleWarfareAid(Player* player);
	bool OnlineArenaTeamMember(Player* player, uint32 arenaType);
	bool OnlineGuildMember(Player* player);
};

#define sPluginCommand PluginCommand::instance()

#endif // !_PLUGIN_COMMAND_H_
