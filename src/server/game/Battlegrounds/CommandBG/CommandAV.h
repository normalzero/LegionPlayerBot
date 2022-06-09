
#ifndef _COMMAND_AV_H_
#define _COMMAND_AV_H_

#include "CommandBG.h"
#include "BattlegroundAlteracValley.h"

#define AIWP_LM_GENERAL 0
#define AIWP_LM_GENERAL_GRAVE 1
#define AIWP_LM_NORTH_TOWER 2
#define AIWP_LM_SOUTH_TOWER 3
#define AIWP_LM_PIKE_GRAVE 4
#define AIWP_LM_ICEWING_TOWER 5
#define AIWP_LM_STONE_TOWER 6
#define AIWP_LM_STONE_GRAVE 7
#define AIWP_LM_WOMEN_POINT 8
#define AIWP_SNOWFALL_GRAVE 9
#define AIWP_BL_ICEBLOOD_GRAVE 10
#define AIWP_BL_MAN_POINT 11
#define AIWP_BL_ICEBLOOD_TOWER 12
#define AIWP_BL_HIGH_POWER 13
#define AIWP_BL_FROSTWOLF_GRAVE 14
#define AIWP_BL_EAST_POWER 15
#define AIWP_BL_WEST_POWER 16
#define AIWP_BL_GENERAL_GRAVE 17
#define AIWP_BL_GENERAL 18
#define AIWP_BL_CENTER_POINT 19
#define AIWP_LM_AVSTART_POINT 20
#define AIWP_BL_AVSTART_POINT 21

class CommandAV : public CommandBG
{
public:
	CommandAV(Battleground* pBG, TeamId team);
	~CommandAV();

	void Initialize() override;
	void StartGame() override;
	bool AddPlayerBot(Player* player, Battleground* pBG) override;
	const Creature* GetMatchGraveyardNPC(const Player* player) override;
	void Update(uint32 diff) override;
	AIWaypoint* GetReadyPosition() override;
	bool CanDireFlee() override;

private:
	void RndStartCommand();
	void ProcessRegulation();

	void ProcessAssault(BattlegroundAV* pBattlegroundAV, PlayerGUIDs& allPlayers);
	void ProcessDefense(BattlegroundAV* pBattlegroundAV, PlayerGUIDs& allPlayers);
	void ProcessAttackGeneral(BattlegroundAV* pBattlegroundAV, PlayerGUIDs& allPlayers);

	void ProcessRequirementByNodeType(BattlegroundAV* pBattlegroundAV, BG_AV_Nodes nodeType, AIWaypoint* waypoint, PlayerGUIDs& allPlayers);
	void ProcessRequirementByCaptain(BattlegroundAV* pBattlegroundAV, TeamId team, AIWaypoint* waypoint, PlayerGUIDs& allPlayers, bool attOrDef);
	PlayerGUIDs GetAVFlagRangePlayerByTeam(BattlegroundAV* pBattlegroundAV, BG_AV_Nodes nodeType, TeamId team);
	PlayerGUIDs GetAVCaptainRangePlayerByTeam(BattlegroundAV* pBattlegroundAV, Creature const* pCaptain, TeamId team);
	bool AcceptCommandByPlayerGUID(uint64 guid, AIWaypoint* targetAIWP, bool isFlag = false);
	bool AcceptCommandByPlayerGUID(uint64 guid, ObjectGuid flagGuid, bool isFlag = false);
	void TryOccupiedAVNode(BattlegroundAV* pBattlegroundAV, uint64 guid);

private:
	int32 lastUpdateTick;
	bool m_IsStartGeneral;
};

#endif // !_COMMAND_AV_H_
