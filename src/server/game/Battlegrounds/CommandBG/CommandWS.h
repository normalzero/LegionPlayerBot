
#ifndef _COMMAND_WS_H_
#define _COMMAND_WS_H_

#include "CommandBG.h"

#define AIWP_SELF_FLAG 0
#define AIWP_SELF_PORT1 1
#define AIWP_SELF_PORT2 2
#define AIWP_SELF_PORT3 3
#define AIWP_ENEMY_FLAG 4
#define AIWP_ENEMY_PORT1 5
#define AIWP_ENEMY_PORT2 6
#define AIWP_ENEMY_PORT3 7

#define MAX_WSAIWP_COUNT 8

typedef std::vector<BGCommandInfo> BGCommands;

class CommandWS : public CommandBG
{
public:
	CommandWS(Battleground* pBG, TeamId team);
	~CommandWS();

	void Initialize() override;
	const Creature* GetMatchGraveyardNPC(const Player* player) override;
	void Update(uint32 diff) override;
	bool CanUpMount(Player* player) override;

private:
	void InsureAttackAndDefance(uint32 count);
	void RndStartCommand();
	void ProcessRegulation();

	void ProcessAllPicked();
	void ProcessAllGuared(ObjectGuid guaredGuid);
	void ProcessAllAttack(ObjectGuid attackGuid);
	void ProcessAttackAndGuard(ObjectGuid attackGuid, ObjectGuid guaredGuid);

	void TryPickEnemyFlag(uint64 guid);
	void TryPickSelfFlag(uint64 guid);
	void TryCaptureFlag(uint64 guid);
	ObjectGuid SelfBGFlagPicker();
	ObjectGuid EnemyBGFlagPicker();
	ObjectGuid GetBGFlagFromSelf();
	ObjectGuid GetBGFlagFromEnemy();

	GameObject* SearchDropedFlag(uint64 guid, TeamId team);

private:
	int32 lastUpdateTick;
};

#endif // !_COMMAND_WS_H_
