#ifndef SC_BOTFOLLOWERAI_H
#define SC_BOTFOLLOWERAI_H

#include "ScriptSystem.h"
#include "PlayerAI.h"
#include "Pathfinding.h"
#include "BotAITool.h"

class  BotMovementAI : public PlayerAI
{
public:
	explicit BotMovementAI(Player* player) : PlayerAI(player), m_UpdateTick(BOTAI_UPDATE_TICK), pHorrorState(NULL)
	{
	}
	~BotMovementAI()
	{
	}

	void DamageDealt(Unit* victim, uint32& damage, DamageEffectType damageType) override;
	void DamageEndure(Unit* attacker, uint32& damage, DamageEffectType damageType);
	void UpdateAI(uint32 diff) override;
	void MovementTo(Player* player);
	void MovementTo(float x, float y, float z);
	void MovementToPath(Player* player, uint32 pid, uint32 index);
	void ApplyFinishPath(PathParameter* pathParam);

private:
	bool HasAuraMechanic(Unit* pTarget, Mechanics mask);
	void ProcessHorror(uint32 diff);

private:
	int32 m_UpdateTick;
	BotAIHorrorState* pHorrorState;
};

#endif // SC_BOTFOLLOWERAI_H
