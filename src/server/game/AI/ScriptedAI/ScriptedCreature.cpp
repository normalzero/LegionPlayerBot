/* Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 *
 * Thanks to the original authors: ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
 *
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#include "BrawlersGuild.h"
#include "Cell.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectMgr.h"
#include "ObjectVisitors.hpp"
#include "Packets/InstancePackets.h"
#include "ScriptedCreature.h"
#include "Spell.h"
#include "BotGroupAI.h"
#include "Group.h"
#include <corecrt_math_defines.h>

 // Spell summary for ScriptedAI::SelectSpell
extern struct TSpellSummary
{
    uint8 Targets;                                          // set of enum SelectTarget
    uint8 Effects;                                          // set of enum SelectEffect
};
extern TSpellSummary* SpellSummary;

void SummonList::DoZoneInCombat(uint32 entry)
{
    for (iterator i = begin(); i != end();)
    {
        Creature* summon = Unit::GetCreature(*me, *i);
        ++i;
        if (summon && summon->IsAIEnabled && (!entry || summon->GetEntry() == entry))
            summon->AI()->DoZoneInCombat();
    }
}

void SummonList::DespawnEntry(uint32 entry)
{
    std::lock_guard<std::recursive_mutex> guard(m_lock);
    for (iterator i = begin(); i != end();)
    {
        Creature* summon = Unit::GetCreature(*me, *i);
        if (!summon)
            erase(i++);
        else if (summon->GetEntry() == entry)
        {
            erase(i++);
            summon->DespawnOrUnsummon();
        }
        else
            ++i;
    }
}

void SummonList::DespawnAll()
{
    std::lock_guard<std::recursive_mutex> guard(m_lock);
    while (!empty())
    {
        Creature* summon = Unit::GetCreature(*me, *begin());
        if (!summon || summon->HasUnitTypeMask(UNIT_MASK_ACCESSORY))
            erase(begin());
        else
        {
            erase(begin());
            summon->DespawnOrUnsummon();
        }
    }
}

void SummonList::SetReactState(ReactStates state)
{
    for (iterator i = begin(); i != end();)
    {
        Creature* summon = Unit::GetCreature(*me, *i++);
        if (summon && summon->IsAIEnabled)
            summon->SetReactState(state);
    }
}

void SummonList::RemoveNotExisting()
{
    for (iterator i = begin(); i != end();)
    {
        if (Unit::GetCreature(*me, *i))
            ++i;
        else
            erase(i++);
    }
}

bool SummonList::HasEntry(uint32 entry)
{
    for (iterator i = begin(); i != end();)
    {
        Creature* summon = Unit::GetCreature(*me, *i);
        if (!summon)
            erase(i++);
        else if (summon->GetEntry() == entry)
            return true;
        else
            ++i;
    }

    return false;
}

void SummonList::KillAll()
{
    std::lock_guard<std::recursive_mutex> guard(m_lock);
    for (auto & i : *this)
    {
        if (Creature* summon = Unit::GetCreature(*me, i))
        {
            summon->Kill(summon);
            summon->DespawnOrUnsummon(500);
        }
    }
}

void SummonList::Summon(Creature* summon)
{
    if (summon->HasUnitTypeMask(UNIT_MASK_ACCESSORY))
        return;

    push_back(summon->GetGUID());
}

Creature* SummonList::GetCreature(uint32 entry)
{
    for (auto & i : *this)
    {
        if (Creature* summon = Unit::GetCreature(*me, i))
            if (summon->GetEntry() == entry)
                return summon;
    }

    return nullptr;
}

// GO
void SummonListGO::DespawnEntry(uint32 entry)
{
    std::lock_guard<std::recursive_mutex> guard(m_lock_go);
    for (auto i = begin(); i != end();)
    {
        GameObject* summon = Unit::GetGameObjectOnMap(*me, *i);
        if (!summon)
            erase(i++);
        else if (summon->GetEntry() == entry)
        {
            erase(i++);
            summon->Delete();
        }
        else
            ++i;
    }
}

void SummonListGO::DespawnAll()
{
    std::lock_guard<std::recursive_mutex> guard(m_lock_go);
    while (!empty())
    {
        GameObject* summon = Unit::GetGameObjectOnMap(*me, *begin());
        if (!summon)
            erase(begin());
        else
        {
            erase(begin());
            summon->Delete();
        }
    }
}

void SummonListGO::RemoveNotExisting()
{
    for (auto i = begin(); i != end();)
    {
        if (Unit::GetGameObjectOnMap(*me, *i))
            ++i;
        else
            erase(i++);
    }
}

bool SummonListGO::HasEntry(uint32 entry)
{
    for (auto i = begin(); i != end();)
    {
        GameObject* summon = Unit::GetGameObjectOnMap(*me, *i);
        if (!summon)
            erase(i++);
        else if (summon->GetEntry() == entry)
            return true;
        else
            ++i;
    }

    return false;
}

ScriptedAI::ScriptedAI(Creature* creature) : CreatureAI(creature),
me(creature),
IsFleeing(false),
_evadeCheckCooldown(2500),
_checkHomeTimer(5000),
_isCombatMovementAllowed(true)
{
    _isHeroic = me->GetMap()->IsHeroic();
    _difficulty = Difficulty(me->GetMap()->GetSpawnMode());
}

bool ScriptedAI::HealthBelowPct(uint32 pct) const
{
    return me->HealthBelowPct(pct);
}

bool ScriptedAI::HealthAbovePct(uint32 pct) const
{
    return me->HealthAbovePct(pct);
}

float ScriptedAI::GetHealthPct(uint32 damage) const
{
    return damage > me->GetHealth() ? 0.0f : 100.0f * float(float(me->GetHealth() - damage) / me->GetMaxHealth());
}

float ScriptedAI::GetHealthPctWithHeal(uint32 heal) const
{
    return 100.0f * float(float(me->GetHealth() + heal) / me->GetMaxHealth());
}

bool ScriptedAI::IsCombatMovementAllowed() const
{
    return _isCombatMovementAllowed;
}

bool ScriptedAI::IsHeroic() const
{
    return _isHeroic;
}

Difficulty ScriptedAI::GetDifficultyID() const
{
    return _difficulty;
}

bool ScriptedAI::Is25ManRaid() const
{
    return _difficulty == DIFFICULTY_25_N || _difficulty == DIFFICULTY_25_HC;
}

bool ScriptedAI::IsLfrRaid() const
{
    return _difficulty == DIFFICULTY_LFR_RAID;
}

bool ScriptedAI::IsNormalRaid() const
{
    return _difficulty == DIFFICULTY_NORMAL_RAID;
}

bool ScriptedAI::IsHeroicRaid() const
{
    return _difficulty == DIFFICULTY_HEROIC_RAID;
}

bool ScriptedAI::IsMythicRaid() const
{
    return _difficulty == DIFFICULTY_MYTHIC_RAID;
}

bool ScriptedAI::IsHeroicPlusRaid() const
{
    return _difficulty == DIFFICULTY_HEROIC_RAID || _difficulty == DIFFICULTY_MYTHIC_RAID;
}

void ScriptedAI::AttackStartNoMove(Unit* who)
{
    if (!who)
        return;

    if (me->Attack(who, false))
        DoStartNoMovement(who);
}

void ScriptedAI::AttackStart(Unit* who)
{
    if (!IsCombatMovementAllowed())
    {
        AttackStartNoMove(who);
        return;
    }

    if (who && me->Attack(who, true))
    {
        if (me->GetCasterPet())
        {
            if (!me->IsWithinMeleeRange(who, me->GetAttackDist()))
                me->GetMotionMaster()->MoveChase(who, me->GetAttackDist() - 0.5f);
        }
        else
            me->GetMotionMaster()->MoveChase(who);
    }
}

void ScriptedAI::UpdateAI(uint32 /*diff*/)
{
    //Check if we have a current target
    if (!UpdateVictim())
        return;

    DoMeleeAttackIfReady();
}

void ScriptedAI::DoStartMovement(Unit* victim, float distance, float angle)
{
    if (victim)
        me->GetMotionMaster()->MoveChase(victim, distance, angle);
}

void ScriptedAI::DoStopAttack()
{
    me->AttackStop();
    me->SetReactState(REACT_PASSIVE);
}

void ScriptedAI::DoStartNoMovement(Unit* victim)
{
    if (!victim)
        return;

    me->GetMotionMaster()->MoveIdle();
}

void ScriptedAI::DoCastSpell(Unit* target, SpellInfo const* spellInfo, bool triggered)
{
    if (!target || me->IsNonMeleeSpellCast(false))
        return;

    me->StopMoving();
    me->CastSpell(target, spellInfo, triggered ? TRIGGERED_FULL_MASK : TRIGGERED_NONE);
}

void ScriptedAI::DoPlaySoundToSet(WorldObject* source, uint32 soundId)
{
    if (source)
        source->PlayDirectSound(soundId);
}

Creature* ScriptedAI::DoSpawnCreature(uint32 entry, float offsetX, float offsetY, float offsetZ, float angle, uint32 type, uint32 despawntime)
{
    return me->SummonCreature(entry, me->GetPositionX() + offsetX, me->GetPositionY() + offsetY, me->GetPositionZ() + offsetZ, angle, TempSummonType(type), despawntime);
}

void ScriptedAI::SummonCreatureDelay(uint32 delayTimer, uint32 entry, float x, float y, float z, float orient, TempSummonType spawnType, uint32 despawnTime)
{
    me->AddDelayedCombat(delayTimer, [this, entry, x, y, z, orient, spawnType, despawnTime]() -> void
    {
        if (me)
            me->SummonCreature(entry, x, y, z, orient, spawnType, despawnTime);
    });
}

SpellInfo const* ScriptedAI::SelectSpell(Unit* target, uint32 school, uint32 mechanic, SelectTargetType targets, uint32 powerCostMin, uint32 powerCostMax, float rangeMin, float rangeMax, SelectEffect effects)
{
    //No target so we can't cast
    if (!target)
        return nullptr;

    //Silenced so we can't cast
    if (me->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SILENCED))
        return nullptr;

    //Using the extended script system we first create a list of viable spells
    SpellInfo const* apSpell[CREATURE_MAX_SPELLS];
    memset(apSpell, 0, CREATURE_MAX_SPELLS * sizeof(SpellInfo*));

    uint32 spellCount = 0;

    //Check if each spell is viable(set it to null if not)
    for (auto spellID : me->m_templateSpells)
    {
        auto tempSpell = sSpellMgr->GetSpellInfo(spellID);
        if (!tempSpell)
            continue;

        // Targets and Effects checked first as most used restrictions
        //Check the spell targets if specified
        if (targets && !(SpellSummary[spellID].Targets & (1 << (targets - 1))))
            continue;

        //Check the type of spell if we are looking for a specific spell type
        if (effects && !(SpellSummary[spellID].Effects & (1 << (effects - 1))))
            continue;

        //Check for school if specified
        if (school && (tempSpell->GetMisc()->MiscData.SchoolMask & school) == 0)
            continue;

        //Check for spell mechanic if specified
        if (mechanic && tempSpell->Categories.Mechanic != mechanic)
            continue;

        //Make sure that the spell uses the requested amount of power
        if (powerCostMin && static_cast<uint32>(tempSpell->Power.PowerCost) < static_cast<uint32>(powerCostMin))
            continue;

        if (powerCostMax && static_cast<uint32>(tempSpell->Power.PowerCost) > static_cast<uint32>(powerCostMax))
            continue;

        //Continue if we don't have the mana to actually cast this spell
        if (static_cast<uint32>(tempSpell->Power.PowerCost) > static_cast<uint32>(me->GetPower(Powers(tempSpell->Power.PowerType))))
            continue;

        //Check if the spell meets our range requirements
        if (rangeMin && me->GetSpellMinRangeForTarget(target, tempSpell) < rangeMin)
            continue;
        if (rangeMax && me->GetSpellMaxRangeForTarget(target, tempSpell) > rangeMax)
            continue;

        //Check if our target is in range
        if (me->IsWithinDistInMap(target, float(me->GetSpellMinRangeForTarget(target, tempSpell))) || !me->IsWithinDistInMap(target, float(me->GetSpellMaxRangeForTarget(target, tempSpell))))
            continue;

        //All good so lets add it to the spell list
        apSpell[spellCount] = tempSpell;
        ++spellCount;
    }

    //We got our usable spells so now lets randomly pick one
    if (!spellCount)
        return nullptr;

    return apSpell[urand(0, spellCount - 1)];
}

void ScriptedAI::DoResetThreat()
{
    if (!me->CanHaveThreatList() || me->getThreatManager().isThreatListEmpty())
    {
        TC_LOG_ERROR(LOG_FILTER_TSCR, "DoResetThreat called for creature that either cannot have threat list or has empty threat list (me entry = %d)", me->GetEntry());
        return;
    }

    std::list<HostileReference*>& threatlist = me->getThreatManager().getThreatList();

    for (auto & itr : threatlist)
    {
        Unit* unit = Unit::GetUnit(*me, itr->getUnitGuid());

        if (unit && DoGetThreat(unit))
            DoModifyThreatPercent(unit, -100);
    }
}

float ScriptedAI::DoGetThreat(Unit* unit)
{
    if (!unit)
        return 0.0f;
    return me->getThreatManager().getThreat(unit);
}

void ScriptedAI::DoModifyThreatPercent(Unit* unit, int32 pct)
{
    if (!unit)
        return;
    me->getThreatManager().modifyThreatPercent(unit, pct);
}

void ScriptedAI::DoTeleportTo(float x, float y, float z, uint32 time)
{
    me->Relocate(x, y, z);
    float speed = me->GetDistance(x, y, z) / (static_cast<float>(time) * 0.001f);
    me->MonsterMoveWithSpeed(x, y, z, speed);
}

void ScriptedAI::DoTeleportTo(const float position[4])
{
    me->NearTeleportTo(position[0], position[1], position[2], position[3]);
}

void ScriptedAI::DoTeleportPlayer(Unit* unit, float x, float y, float z, float o)
{
    if (!unit)
        return;

    if (Player* player = unit->ToPlayer())
        player->TeleportTo(unit->GetMapId(), x, y, z, o, TELE_TO_NOT_LEAVE_COMBAT);
    else
        TC_LOG_ERROR(LOG_FILTER_TSCR, "Creature " UI64FMTD " (Entry: %u) Tried to teleport non-player unit (Type: %u GUID: " UI64FMTD ") to x: %f y:%f z: %f o: %f. Aborted.", me->GetGUID().GetCounter(), me->GetEntry(), unit->GetTypeId(), unit->GetGUID().GetCounter(), x, y, z, o);
}

void ScriptedAI::DoTeleportAll(float x, float y, float z, float o)
{
    Map* map = me->GetMap();
    if (!map->IsDungeon())
        return;

    map->ApplyOnEveryPlayer([&](Player* player)
    {
        if (player->isAlive())
            player->TeleportTo(me->GetMapId(), x, y, z, o, TELE_TO_NOT_LEAVE_COMBAT);
    });
}

Unit* ScriptedAI::DoSelectLowestHpFriendly(float range, uint32 minHPDiff)
{
    Unit* unit = nullptr;
    Trinity::MostHPMissingInRange u_check(me, range, minHPDiff);
    Trinity::UnitLastSearcher<Trinity::MostHPMissingInRange> searcher(me, unit, u_check);
    Trinity::VisitNearbyObject(me, range, searcher);

    return unit;
}

std::list<Creature*> ScriptedAI::DoFindFriendlyCC(float range)
{
    std::list<Creature*> list;
    Trinity::FriendlyCCedInRange u_check(me, range);
    Trinity::CreatureListSearcher<Trinity::FriendlyCCedInRange> searcher(me, list, u_check);
    Trinity::VisitNearbyObject(me, range, searcher);
    return list;
}

std::list<Creature*> ScriptedAI::DoFindFriendlyMissingBuff(float range, uint32 uiSpellid)
{
    std::list<Creature*> list;
    Trinity::FriendlyMissingBuffInRange u_check(me, range, uiSpellid);
    Trinity::CreatureListSearcher<Trinity::FriendlyMissingBuffInRange> searcher(me, list, u_check);
    Trinity::VisitNearbyObject(me, range, searcher);
    return list;
}

Player* ScriptedAI::GetPlayerAtMinimumRange(float minimumRange)
{
    Player* player = nullptr;

    CellCoord pair(Trinity::ComputeCellCoord(me->GetPositionX(), me->GetPositionY()));
    Cell cell(pair);
    cell.SetNoCreate();

    Trinity::PlayerAtMinimumRangeAway check(me, minimumRange);
    Trinity::PlayerSearcher<Trinity::PlayerAtMinimumRangeAway> searcher(me, player, check);

    cell.Visit(pair, Trinity::makeGridVisitor(searcher), *me->GetMap(), *me, minimumRange);

    return player;
}

void ScriptedAI::SetEquipmentSlots(bool loadDefault, int32 mainHand /*= EQUIP_NO_CHANGE*/, int32 offHand /*= EQUIP_NO_CHANGE*/, int32 ranged /*= EQUIP_NO_CHANGE*/)
{
    if (loadDefault)
    {
        me->LoadEquipment(me->GetOriginalEquipmentId(), true);
        return;
    }

    if (mainHand >= 0)
        me->SetVirtualItem(0, uint32(mainHand));

    if (offHand >= 0)
        me->SetVirtualItem(1, uint32(offHand));

    if (ranged >= 0)
        me->SetVirtualItem(2, uint32(ranged));
}

void ScriptedAI::SetCombatMovement(bool allowMovement)
{
    if (!allowMovement && !me->IsInEvadeMode())
        me->GetMotionMaster()->Clear();

    _isCombatMovementAllowed = allowMovement;
}

enum eNPCs
{
    NPC_BROODLORD       = 12017,
    NPC_VOID_REAVER     = 19516,
    NPC_JAN_ALAI        = 23578,
    NPC_SARTHARION      = 28860,
    NPC_ROOK_STONETOE   = 71475,
    NPC_HE_SOFTFOOT     = 71479,
    NPC_SUN_TENDERHEART = 71480,
    NPC_GORASHAN        = 76413,
    NPC_KYRAK           = 76021,
};

// Hacklike storage used for misc creatures that are expected to evade of outside of a certain area.
// It is assumed the information is found elswehere and can be handled by the core. So far no luck finding such information/way to extract it.
bool ScriptedAI::EnterEvadeIfOutOfCombatArea(uint32 const diff)
{
    if (_evadeCheckCooldown <= diff)
        _evadeCheckCooldown = 2500;
    else
    {
        _evadeCheckCooldown -= diff;
        return false;
    }

    if (me->IsInEvadeMode() || !me->getVictim())
        return false;

    float x = me->GetPositionX();
    float y = me->GetPositionY();
    float z = me->GetPositionZ();

    switch (me->GetEntry())
    {
    case NPC_BROODLORD:                                         // broodlord (not move down stairs)
        if (z > 448.60f)
            return false;
        break;
    case NPC_VOID_REAVER:                                         // void reaver (calculate from center of room)
        if (me->GetDistance2d(432.59f, 371.93f) < 105.0f)
            return false;
        break;
    case NPC_JAN_ALAI:                                         // jan'alai (calculate by Z)
        if (z > 12.0f)
            return false;
        break;
    case NPC_SARTHARION:                                         // sartharion (calculate box)
        if (x > 3218.86f && x < 3275.69f && y < 572.40f && y > 484.68f)
            return false;
        break;
    case NPC_ROOK_STONETOE:
    case NPC_HE_SOFTFOOT:
    case NPC_SUN_TENDERHEART:
        if (x > 1162.0f && x < 1258.0f && y > 992.0f && y < 1080.0f && z > 410.0f && z < 425.0f)
            return false;
        break;
    case NPC_GORASHAN:
    case NPC_KYRAK:
        if (me->GetDistance(me->GetHomePosition()) <= 40.0f)
            return false;
        break;
    default: // For most of creatures that certain area is their home area.
        TC_LOG_INFO(LOG_FILTER_GENERAL, "TSCR: EnterEvadeIfOutOfCombatArea used for creature entry %u, but does not have any definition. Using the default one.", me->GetEntry());
        uint32 homeAreaId = me->GetMap()->GetAreaId(me->GetHomePosition().GetPositionX(), me->GetHomePosition().GetPositionY(), me->GetHomePosition().GetPositionZ());
        if (me->GetAreaId() == homeAreaId)
            return false;
    }

    EnterEvadeMode();
    return true;
}

bool ScriptedAI::CheckHomeDistToEvade(uint32 diff, float dist, float x, float y, float z, bool onlyZ)
{
    if (!me->isInCombat())
        return false;

    bool evade = false;

    if (_checkHomeTimer <= diff)
    {
        _checkHomeTimer = 1500;

        if (onlyZ)
        {
            if ((me->GetPositionZ() > z + dist) || (me->GetPositionZ() < z - dist))
                evade = true;
        }
        else if (x != 0.0f || y != 0.0f || z != 0.0f)
        {
            if (me->GetDistance(x, y, z) >= dist)
                evade = true;
        }
        else if (me->GetDistance(me->GetHomePosition()) >= dist)
            evade = true;

        if (evade)
        {
            EnterEvadeMode();
            return true;
        }
    }
    else
    {
        _checkHomeTimer -= diff;
    }

    return false;
}

void ScriptedAI::InitializeAI()
{
	if (PetStats const* pStats = sObjectMgr->GetPetStats(me->GetEntry()))
	{
		if(me->GetEntry() != 100868)
			me->SetReactState(ReactStates(pStats->state));
		else
			me->SetReactState(REACT_ATTACK_OFF);
	}

    CreatureAI::InitializeAI();
}

void ScriptedAI::GetInViewBotPlayers(std::list<Player*>& outPlayers, float range)
{
    Map::PlayerList const& players = me->GetMap()->GetPlayers();
    for (Map::PlayerList::const_iterator i = players.begin(); i != players.end(); ++i)
    {
        Player* player = i->getSource();
        if (!player || !player->isAlive() || !player->IsPlayerBot())
            continue;
        if (!me->InSamePhase(player->GetPhaseMask()))
            continue;
        if (me->GetDistance(player->GetPosition()) > range)
            continue;
        if (!me->IsWithinLOSInMap(player))
            continue;
        outPlayers.push_back(player);
    }
}

void ScriptedAI::SearchTargetPlayerAllGroup(std::list<Player*>& players, float range)
{
    if (range < 3.0f)
        return;
    ObjectGuid targetGUID = me->GetTargetGUID();
    Player* targetPlayer = NULL;
    if (targetGUID == ObjectGuid::Empty)
    {
        std::list<Player*> playersNearby;
        me->GetPlayerListInGrid(playersNearby, range);
        if (playersNearby.empty())
            return;
        for (Player* p : playersNearby)
        {
            if (p->isAlive() && p->GetMap() == me->GetMap())
            {
                targetPlayer = p;
                targetGUID = p->GetGUID();
                break;
            }
        }
    }
    if (!targetPlayer)
        targetPlayer = ObjectAccessor::FindPlayer(targetGUID);
    if (!targetPlayer || targetPlayer->GetMap() != me->GetMap())
        return;
    players.clear();
    players.push_back(targetPlayer);

    Group* pGroup = targetPlayer->GetGroup();
    if (!pGroup || pGroup->isBFGroup())
        return;
    Group::MemberSlotList const& memList = pGroup->GetMemberSlots();
    for (Group::MemberSlot const& slot : memList)
    {
        Player* player = ObjectAccessor::FindPlayer(slot.Guid);
        if (!player || !player->isAlive() || targetPlayer->GetMap() != player->GetMap() ||
            !player->IsInWorld() || player == targetPlayer || !player->IsPlayerBot())
            continue;
        if (me->GetDistance(player->GetPosition()) > range)
            continue;
        players.push_back(player);
    }
}

void ScriptedAI::PickBotPullMeToPosition(Position pullPos, ObjectGuid fliterTarget)
{
    std::list<BotGroupAI*> tankAIs;// , healAIs;
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, 120);
    BotGroupAI* pNearTankAI = NULL;
    float nearTankAIDist = 999999;
    BotGroupAI* pNearHealAI[3] ={ NULL };
    float nearHealAIDist[3] ={ 999999 };
    for (Player* player : targets)
    {
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            if (pGroupAI->IsHealerBotAI())
            {
                float thisDist = me->GetDistance(player->GetPosition());
                for (int i = 0; i < 3; i++)
                {
                    if (thisDist < nearHealAIDist[i])
                    {
                        pNearHealAI[i] = pGroupAI;
                        nearHealAIDist[i] = thisDist;
                        break;
                    }
                }
            }
            else if (pGroupAI->IsTankBotAI())
            {
                if (fliterTarget != ObjectGuid::Empty)
                {
                    Unit* tankTarget = player->GetSelectedUnit();
                    if (tankTarget != NULL && tankTarget->GetGUID() == fliterTarget)
                    {
                        if (pNearTankAI == NULL)
                        {
                            if (urand(0, 99) > 50)
                                continue;
                        }
                        else
                            continue;
                    }
                }
                tankAIs.push_back(pGroupAI);
                float thisDist = me->GetDistance(player->GetPosition());
                if (thisDist < nearTankAIDist)
                {
                    pNearTankAI = pGroupAI;
                    nearTankAIDist = thisDist;
                }
            }
        }
    }
    if (pNearTankAI == NULL)
        return;
    for (BotGroupAI* pGroupAI : tankAIs)
    {
        if (pGroupAI == pNearTankAI)
        {
            pGroupAI->ClearTankTarget();
            pGroupAI->AddTankTarget(me);
            pGroupAI->SetTankPosition(pullPos);
            pGroupAI->GetAIPayer()->SetSelection(me->GetGUID());
            for (int i = 0; i < 3; i++)
            {
                if (pNearHealAI[i])
                    pNearHealAI[i]->GetAIPayer()->SetSelection(me->GetGUID());
            }
        }
        else
        {
            pGroupAI->ClearTankTarget();
            pGroupAI->GetAIPayer()->SetSelection(ObjectGuid::Empty);
        }
    }
}

bool ScriptedAI::ExistPlayerBotByRange(float range)
{
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, range);
    return targets.size() > 0;
}

void ScriptedAI::BotBlockCastingMe()
{
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, 120);
    for (Player* player : targets)
    {
        if (player->HasUnitState(UNIT_STATE_CASTING))
            continue;
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            if (pGroupAI->TryBlockCastingByTarget(me))
                return;
        }
    }
}

void ScriptedAI::ClearBotMeTarget(bool all)
{
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, 120);
    for (Player* player : targets)
    {
        if (Pet* pPet = player->GetPet())
        {
            if (pPet->getVictim() == me)
            {
                if (WorldSession* pSession = player->GetSession())
                {
                    pSession->HandlePetActionHelper(pPet, pPet->GetGUID(), 1, 7, ObjectGuid::Empty);
                }
            }
        }
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            if (all || !pGroupAI->IsTankBotAI())
            {
                player->SetSelection(ObjectGuid::Empty);
                pGroupAI->ToggleFliterCreature(me, true);
            }
        }
    }
}

void ScriptedAI::BotAllMovetoFarByDistance(Unit* pUnit, float range, float dist, float offset)
{
    float onceAngle = float(M_PI) * 2.0f / 8.0f;
    std::list<Position> allPosition;
    for (float angle = 0.0f; angle < (float(M_PI) * 2.0f); angle += onceAngle)
    {
        Position pos;
        pUnit->GetFirstCollisionPosition(pos, dist, angle);
        float dist = pUnit->GetDistance(pos);
        if (!pUnit->IsWithinLOS(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ()))
            continue;
        allPosition.push_back(pos);
    }
    if (allPosition.empty())
        return;
    Position targetPos;
    float maxDist = 0.0f;
    for (Position pos : allPosition)
    {
        float distance = pUnit->GetDistance(pos);
        if (distance > maxDist)
        {
            maxDist = distance;
            targetPos = pos;
        }
    }
    if (maxDist < dist * 0.1f)
        return;
    targetPos.m_positionZ = pUnit->GetMap()->GetHeight(pUnit->GetPhaseMask(), targetPos.GetPositionX(), targetPos.GetPositionY(), targetPos.GetPositionZ());
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, range);
    for (Player* player : targets)
    {
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            Position offsetPos = Position(targetPos.GetPositionX() + frand(-offset, offset),
                targetPos.GetPositionY() + frand(-offset, offset), targetPos.GetPositionZ());
            pGroupAI->ClearCruxMovement();
            pGroupAI->SetCruxMovement(targetPos);
        }
    }
}

void ScriptedAI::BotCruxFlee(uint32 durTime, ObjectGuid fliter)
{
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, 120);
    for (Player* player : targets)
    {
        if (fliter != ObjectGuid::Empty)
        {
            if (player->GetGUID() == fliter)
                continue;
        }
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            pGroupAI->AddCruxFlee(durTime, me);
        }
    }
}

void ScriptedAI::BotRndCruxMovement(float dist)
{
    if (dist < 1.0f)
        return;
    std::list<Player*> playersNearby;
    me->GetPlayerListInGrid(playersNearby, 120);
    if (playersNearby.empty())
        return;
    for (Player* player : playersNearby)
    {
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            pGroupAI->RndCruxMovement(dist);
        }
    }
}

void ScriptedAI::BotCruxFleeByRange(float range)
{
    if (range < 3.0f)
        return;
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, range);
    for (Player* player : targets)
    {
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            Position targetPos;
            if (!BotUtility::FindFirstCollisionPosition(player, range, me, targetPos))
                continue;
            //if (pGroupAI->GetAIPayer()->HasUnitState(UNIT_STATE_CASTING))
            //	pGroupAI->GetAIPayer()->CastStop();
            pGroupAI->SetCruxMovement(targetPos);
            //player->SetSelection(ObjectGuid::Empty);
        }
    }
}

void ScriptedAI::BotCruxFleeByRange(float range, Unit* pCenter)
{
    if (range < 3.0f || !pCenter)
        return;
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, range);
    for (Player* player : targets)
    {
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            Position targetPos;
            if (!BotUtility::FindFirstCollisionPosition(player, range, pCenter, targetPos))
                continue;
            pGroupAI->SetCruxMovement(targetPos);
            //player->SetSelection(ObjectGuid::Empty);
        }
    }
}

void ScriptedAI::BotCruxFleeByArea(float range, float fleeDist, Unit* pCenter)
{
    if (range < 3.0f || fleeDist < 3.0f || !pCenter)
        return;
    Position& centerPos = pCenter->GetPosition();
    std::list<Player*> players;
    SearchTargetPlayerAllGroup(players, 80);
    for (Player* player : players)
    {
        if (player->GetDistance(centerPos) > range)
            continue;
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            Position targetPos;
            if (!BotUtility::FindFirstCollisionPosition(player, fleeDist, pCenter, targetPos))
                continue;
            pGroupAI->SetCruxMovement(targetPos);
            //player->SetSelection(ObjectGuid::Empty);
        }
    }
}

void ScriptedAI::BotAllTargetMe(bool all)
{
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, 120);
    for (Player* player : targets)
    {
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            if (all)
                player->SetSelection(me->GetGUID());
            else if (!pGroupAI->IsTankBotAI() && !pGroupAI->IsHealerBotAI())
                player->SetSelection(me->GetGUID());
        }
    }
}

void ScriptedAI::BotPhysicsDPSTargetMe(Unit* pUnit)
{
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, 120);
    for (Player* player : targets)
    {
        if (!player->IsPlayerBot())
            continue;
        if (player->getClass() == Classes::CLASS_ROGUE || player->getClass() == Classes::CLASS_HUNTER)
            player->SetSelection(me->GetGUID());
        else if (player->getClass() == Classes::CLASS_WARRIOR)
        {
            if (player->FindTalentType() != 2)
                player->SetSelection(pUnit->GetGUID());
        }
        else
        {
            if (player->GetSelectedUnit() == pUnit)
            {
                if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
                {
                    if (!pGroupAI->IsHealerBotAI() && !pGroupAI->IsTankBotAI())
                        player->SetSelection(ObjectGuid::Empty);
                }
            }
        }
    }
}

void ScriptedAI::BotMagicDPSTargetMe(Unit* pUnit)
{
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, 120);
    for (Player* player : targets)
    {
        if (!player->IsPlayerBot())
            continue;
        if (player->getClass() == Classes::CLASS_ROGUE || player->getClass() == Classes::CLASS_HUNTER ||
            player->getClass() == Classes::CLASS_WARRIOR)
        {
            if (player->GetSelectedUnit() == pUnit)
            {
                if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
                {
                    if (!pGroupAI->IsHealerBotAI() && !pGroupAI->IsTankBotAI())
                        player->SetSelection(ObjectGuid::Empty);
                }
            }
        }
        else
        {
            if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
            {
                if (!pGroupAI->IsHealerBotAI() && !pGroupAI->IsTankBotAI())
                    player->SetSelection(pUnit->GetGUID());
            }
        }
    }
}

void ScriptedAI::BotAverageCreatureTarget(std::vector<Creature*>& targets, float searchRange)
{
    if (targets.empty() || searchRange < 3.0f)
        return;
    std::queue<Player*> players;
    std::list<Player*> searchUnits;
    SearchTargetPlayerAllGroup(searchUnits, searchRange);
    for (Player* player : searchUnits)
    {
        players.push(player);
    }
    while (!players.empty())
    {
        for (Creature* pCreature : targets)
        {
            Player* player = players.front();
            players.pop();
            if (pCreature)
                player->SetSelection(pCreature->GetGUID());
            if (players.empty())
                break;
        }
    }
}

void ScriptedAI::BotAllotCreatureTarget(std::vector<Creature*>& targets, float searchRange, uint32 onceCount)
{
    if (onceCount < 1 || targets.empty() || searchRange < 3.0f)
        return;
    std::queue<Player*> players;
    std::list<Player*> searchUnits;
    SearchTargetPlayerAllGroup(searchUnits, searchRange);
    for (Player* player : searchUnits)
    {
        players.push(player);
    }
    while (!players.empty())
    {
        for (Creature* pCreature : targets)
        {
            int32 allot = int32(onceCount);
            while (!players.empty() && allot > 0)
            {
                --allot;
                Player* player = players.front();
                players.pop();
                if (pCreature)
                    player->SetSelection(pCreature->GetGUID());
            }
            if (players.empty())
                break;
        }
    }
}

void ScriptedAI::BotAllToSelectionTarget(Unit* pUnit, float searchRange, bool all)
{
    if (!pUnit)
        return;
    ObjectGuid guid = pUnit->GetGUID();
    std::list<Player*> searchUnits;
    SearchTargetPlayerAllGroup(searchUnits, searchRange);
    for (Player* player : searchUnits)
    {
        if (player->GetTargetGUID() == guid)
            continue;
        if (all)
            player->SetSelection(pUnit->GetGUID());
        else if (BotGroupAI* pAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            if (!pAI->IsTankBotAI())
                player->SetSelection(pUnit->GetGUID());
        }
    }
}

void ScriptedAI::BotAllFullDispel(bool enables)
{
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, 120);
    for (Player* player : targets)
    {
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            if (enables)
                pGroupAI->StartFullDispel();
            else
                pGroupAI->ClearFullDispel();
        }
    }
}

void ScriptedAI::BotAllFullDispelByDecPoison(bool enables)
{
    std::list<Player*> targets;
    SearchTargetPlayerAllGroup(targets, 120);
    for (Player* player : targets)
    {
        if (player->getClass() != Classes::CLASS_DRUID && player->getClass() != Classes::CLASS_SHAMAN)
            continue;
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            if (enables)
                pGroupAI->StartFullDispel();
            else
                pGroupAI->ClearFullDispel();
        }
    }
}

void ScriptedAI::BotFleeLineByAngle(Unit* center, float angle, bool force)
{
    angle = Position::NormalizeOrientation(angle);
    std::list<Player*> playersNearby;
    center->GetPlayerListInGrid(playersNearby, center->GetObjectSize() + 80.0f);
    for (Player* player : playersNearby)
    {
        if (!player->IsPlayerBot() || !player->IsInWorld() || player->GetMap() != center->GetMap() || !player->isAlive())
            continue;
        float fleeRange = center->GetDistance(player->GetPosition());
        if (fleeRange <= 0)
            fleeRange = center->GetObjectSize() + 1.0f;
        float pangle = center->GetAngle(&player->GetPosition()) - angle;
        if (pangle >= 0 && pangle <= float(M_PI_4))
        {
            if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
            {
                float fleeAngle = Position::NormalizeOrientation(angle + float(M_PI_4));
                Position fleePos = Position(center->GetPositionX() + fleeRange * std::cosf(fleeAngle),
                    center->GetPositionY() + fleeRange * std::sinf(fleeAngle), player->GetPositionZ(), player->GetOrientation());
                fleePos.m_positionZ = player->GetMap()->GetHeight(player->GetPhaseMask(), fleePos.GetPositionX(), fleePos.GetPositionY(), fleePos.GetPositionZ());
                //if (pGroupAI->GetAIPayer()->HasUnitState(UNIT_STATE_CASTING))
                //	pGroupAI->GetAIPayer()->CastStop();
                if (force)
                    pGroupAI->ClearCruxMovement();
                pGroupAI->SetCruxMovement(fleePos);
            }
        }
        else if (pangle < 0 && pangle >= float(-M_PI_4))
        {
            if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
            {
                float fleeAngle = Position::NormalizeOrientation(angle - float(M_PI_4));
                Position fleePos = Position(center->GetPositionX() + fleeRange * std::cosf(fleeAngle),
                    center->GetPositionY() + fleeRange * std::sinf(fleeAngle), player->GetPositionZ(), player->GetOrientation());
                fleePos.m_positionZ = player->GetMap()->GetHeight(player->GetPhaseMask(), fleePos.GetPositionX(), fleePos.GetPositionY(), fleePos.GetPositionZ());
                //if (pGroupAI->GetAIPayer()->HasUnitState(UNIT_STATE_CASTING))
                //	pGroupAI->GetAIPayer()->CastStop();
                if (force)
                    pGroupAI->ClearCruxMovement();
                pGroupAI->SetCruxMovement(fleePos);
            }
        }
    }
}

void ScriptedAI::BotSwitchPullTarget(Unit* pTarget)
{
    if (!pTarget || !pTarget->ToCreature())
        return;
    Player* pTargetPlayer = ObjectAccessor::FindPlayer(pTarget->GetTargetGUID());
    if (pTargetPlayer && pTargetPlayer->isAlive())
    {
        pTargetPlayer->SetSelection(ObjectGuid::Empty);
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(pTargetPlayer->GetAI()))
            pGroupAI->ClearTankTarget();
    }
    std::list<Player*> playersNearby;
    pTarget->GetPlayerListInGrid(playersNearby, pTarget->GetObjectSize() + 80.0f);
    for (Player* player : playersNearby)
    {
        if (player && player == pTargetPlayer)
            continue;
        if (!player->IsPlayerBot() || !player->IsInWorld() || player->GetMap() != pTarget->GetMap() || !player->isAlive())
            continue;
        if (BotGroupAI* pGroupAI = dynamic_cast<BotGroupAI*>(player->GetAI()))
        {
            if (pGroupAI->IsTankBotAI())
            {
                if (pGroupAI->ProcessPullSpell(pTarget))
                    return;
            }
        }
    }
}

void ScriptedAI::BotVehicleChaseTarget(Unit* pTarget, float distance)
{
    if (!pTarget || !pTarget->ToCreature())
        return;
    std::list<Player*> playersNearby;
    me->GetPlayerListInGrid(playersNearby, distance * 2);
    if (playersNearby.empty())
        return;
    for (Player* bot : playersNearby)
    {
        if (!bot->IsPlayerBot() || !bot->isAlive() || bot->GetMap() != pTarget->GetMap())
            continue;
        if (bot->GetTargetGUID() != pTarget->GetGUID())
            bot->SetSelection(pTarget->GetGUID());
        if (BotGroupAI* pBotAI = dynamic_cast<BotGroupAI*>(bot->GetAI()))
        {
            pBotAI->SetVehicle3DNextMoveGap(8.0f);
            pBotAI->SetVehicle3DMoveTarget(pTarget, distance);
        }
        Unit* vehBase = bot->GetCharm();
        if (!vehBase || !vehBase->isAlive() || vehBase->GetMap() != pTarget->GetMap())
            continue;
        if (vehBase->GetTargetGUID() != pTarget->GetGUID())
            vehBase->SetTarget(pTarget->GetGUID());
        if (vehBase->HasSpell(57092) && !vehBase->HasAura(57092))
            vehBase->CastSpell(vehBase, 57092);
        float power = (float)vehBase->GetPower(POWER_ENERGY) / (float)vehBase->GetMaxPower(POWER_ENERGY);
        if (power >= 0.4f)
        {
            uint8 combo = bot->GetComboPoints();
            if (combo > 4)
            {
                if (vehBase->GetHealthPct() < 75 && urand(0, 99) > 60)
                {
                    if (vehBase->HasSpell(57108) && !vehBase->HasAura(57108) && urand(0, 99) > 60)
                        vehBase->CastSpell(vehBase, 57108);
                    else if (vehBase->HasSpell(57143))
                        vehBase->CastSpell(vehBase, 57143);
                }
                else if (vehBase->HasSpell(56092))
                    vehBase->CastSpell(pTarget, 56092);
            }
            else
            {
                if (vehBase->GetHealthPct() < 75 && vehBase->HasSpell(57090) && urand(0, 99) > 60)
                    vehBase->CastSpell(vehBase, 57090);
                else if (vehBase->HasSpell(56091))
                    vehBase->CastSpell(pTarget, 56091);
            }
        }
    }
}

void ScriptedAI::BotUseGOTarget(GameObject* pGO)
{
    if (!pGO)
        return;
    std::list<Player*> playersNearby;
    me->GetPlayerListInGrid(playersNearby, 100);
    if (playersNearby.empty())
        return;
    ObjectGuid goGUID = pGO->GetGUID();
    std::map<float, Player*> distPlayers;
    for (Player* bot : playersNearby)
    {
        if (!bot->IsPlayerBot() || !bot->isAlive() || bot->GetMap() != pGO->GetMap())
            continue;
        if (bot->HasUnitState(UNIT_STATE_CASTING))
            continue;
        distPlayers[bot->GetDistance(pGO->GetPosition())] = bot;
    }
    for (std::map<float, Player*>::iterator itDist = distPlayers.begin();
        itDist != distPlayers.end();
        itDist++)
    {
        Player* bot = itDist->second;
        if (BotGroupAI* pAI = dynamic_cast<BotGroupAI*>(bot->GetAI()))
        {
            if (pAI->SetMovetoUseGOTarget(goGUID))
                return;
        }
    }
}

bool NeedBotAttackCreature::UpdateProcess(std::list<ObjectGuid>& freeBots)
{
    Creature* pCreature = atMap->GetCreature(needCreature);
    if (!pCreature || !pCreature->isAlive())
    {
        allUsedBots.clear();
        return false;
    }
    if (pCreature && !pCreature->IsVisible())
    {
        allUsedBots.clear();
        return true;
    }

    while (int32(allUsedBots.size()) < needCount)
    {
        if (freeBots.empty())
            break;
        allUsedBots.push_back(*freeBots.begin());
        freeBots.erase(freeBots.begin());
    }
    std::list<std::list<ObjectGuid>::iterator > needClearBot;
    for (std::list<ObjectGuid>::iterator itBot = allUsedBots.begin(); itBot != allUsedBots.end(); itBot++)
    {
        ObjectGuid& guid = *itBot;
        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player || !player->isAlive() || player->GetMap() != atMap)
            needClearBot.push_back(itBot);
        else if (player->GetDistance(pCreature->GetPosition()) < 120)
            player->SetSelection(needCreature);
    }
    for (std::list<ObjectGuid>::iterator itClear : needClearBot)
    {
        allUsedBots.erase(itClear);
    }
    return true;
}

void BotAttackCreature::UpdateNeedAttackCreatures(uint32 diff, ScriptedAI* affiliateAI, bool attackMain)
{
    currentTick -= int32(diff);
    if (currentTick >= 0)
        return;
    currentTick = updateGap;
    if (!mainCreature)
        return;

    if (!affiliateAI)
        return;
    if (allNeedCreatures.empty())
        return;
    std::list<Player*> allBots;
    affiliateAI->SearchTargetPlayerAllGroup(allBots, 120);
    std::list<ObjectGuid> allBotGUIDs;
    for (Player* player : allBots)
    {
        ObjectGuid guid = player->GetGUID();
        bool canPush = true;
        for (NeedBotAttackCreature* pNeed : allNeedCreatures)
        {
            if (pNeed->IsThisUsedBot(guid))
            {
                canPush = false;
                break;
            }
        }
        if (canPush)
            allBotGUIDs.push_back(guid);
    }
    std::list<std::list<NeedBotAttackCreature*>::iterator > needClears;
    for (std::list<NeedBotAttackCreature*>::iterator itNeed = allNeedCreatures.begin(); itNeed != allNeedCreatures.end(); itNeed++)
    {
        NeedBotAttackCreature* pNeed = *itNeed;
        bool ing = pNeed->UpdateProcess(allBotGUIDs);
        if (!ing)
            needClears.push_back(itNeed);
    }
    for (std::list<NeedBotAttackCreature*>::iterator itClear : needClears)
    {
        NeedBotAttackCreature* pNeed = *itClear;
        delete pNeed;
        allNeedCreatures.erase(itClear);
    }
    if (attackMain && mainCreature && mainCreature->isAlive())
    {
        for (ObjectGuid guid : allBotGUIDs)
        {
            Player* player = ObjectAccessor::FindPlayer(guid);
            if (player && player->isAlive() && player->GetMap() == mainCreature->GetMap())
                player->SetSelection(mainCreature->GetGUID());
        }
    }
    else if (attackMain)
    {
        mainCreature = NULL;
    }
}

void BotAttackCreature::AddNewCreatureNeedAttack(Creature* pCreature, int32 needBotCount)
{
    if (!pCreature || !pCreature->isAlive() || needBotCount < 0 || pCreature == mainCreature)
        return;
    if (pCreature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE))
        return;
    Map* atMap = pCreature->GetMap();
    if (!atMap)
        return;
    ObjectGuid guid = pCreature->GetGUID();
    for (NeedBotAttackCreature* pNeed : allNeedCreatures)
    {
        if (pNeed->IsThisCreature(guid))
            return;
    }
    NeedBotAttackCreature* pNeedCreature = new NeedBotAttackCreature(atMap, needBotCount, guid);
    allNeedCreatures.push_back(pNeedCreature);
}

void Scripted_NoMovementAI::AttackStart(Unit* target)
{
    if (!target)
        return;

    if (me->Attack(target, true))
        DoStartNoMovement(target);
}

// BossAI - for instanced bosses

BossAI::BossAI(Creature* creature, uint32 bossId) : ScriptedAI(creature), instance(creature->GetInstanceScript()), summons(creature), _boundary(instance ? instance->GetBossBoundary(bossId) : nullptr), _bossId(bossId), _checkareaTimer(1000), _checkZoneInCombatTimer(5000)
{
    scheduler.SetValidator([this]
    {
        return !me->HasUnitState(UNIT_STATE_CASTING);
    });
}

BossAI::~BossAI()
{
    scheduler.CancelAll();
}

BossBoundaryMap const* BossAI::GetBoundary() const
{
    return _boundary;
}

void BossAI::Reset()
{
    _Reset();
}

void BossAI::EnterCombat(Unit*)
{
    _EnterCombat();
}

void BossAI::JustDied(Unit*)
{
    _JustDied();
}

void BossAI::JustReachedHome()
{
    _JustReachedHome();
}

void BossAI::_JustReachedHome()
{
    me->setActive(false);
}

bool BossAI::CheckInArea(uint32 diff, uint32 areaId)
{
    if (_checkareaTimer <= diff)
        _checkareaTimer = 3000;
    else
    {
        _checkareaTimer -= diff;
        return true;
    }

    if (me->GetAreaId() != areaId)
    {
        EnterEvadeMode();
        return false;
    }

    return true;
}

void BossAI::_Reset()
{
    me->SummonCreatureGroupDespawn(CREATURE_SUMMON_GROUP_COMBAT);
    me->RemoveAllAreaObjects();

    events.Reset();
    scheduler.CancelAll();
    summons.DespawnAll();
    me->SetHealth(me->GetMaxHealth());

    if (instance && me->isAlive())
    {
        instance->SetBossState(_bossId, NOT_STARTED);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_INSTANCE_END, me);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_SET_ALLOWING_RELEASE, me, false);
    }

    me->SummonCreatureGroup(CREATURE_SUMMON_GROUP_RESET);
}

void BossAI::_JustDied()
{
    me->SummonCreatureGroupDespawn(CREATURE_SUMMON_GROUP_RESET);
    me->SummonCreatureGroupDespawn(CREATURE_SUMMON_GROUP_COMBAT);
    me->RemoveAllAreaObjects();
    me->m_Events.KillAllEvents(true);

    events.Reset();
    scheduler.CancelAll();
    summons.DespawnAll();
    if (instance)
    {
        instance->SetBossState(_bossId, DONE);
        instance->SaveToDB();
        instance->SendEncounterUnit(ENCOUNTER_FRAME_DISENGAGE, me);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_INSTANCE_END, me);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_SET_ALLOWING_RELEASE, me, false);
    }
}

void BossAI::_DespawnAtEvade(uint32 delayToRespawn /*= 30*/, Creature* who /*= nullptr*/)
{
    if (delayToRespawn < 2)
        delayToRespawn = 2;

    if (!who)
        who = me;

    who->SetVisible(false);

    who->RemoveAllAurasExceptType(SPELL_AURA_CONTROL_VEHICLE);
    who->DeleteThreatList();
    who->ClearSaveThreatTarget();
    who->CombatStop(true);
    who->LoadCreaturesAddon();
    who->SetLootRecipient(nullptr);
    who->ResetPlayerDamageReq();
    who->GetMotionMaster()->MoveTargetedHome();

    if (instance && who == me)
    {
        instance->SetBossState(_bossId, FAIL);
        instance->LogCompletedEncounter(false);
    }

    AddDelayedEvent(delayToRespawn, [=]() -> void
    {
        who->SetVisible(true);
    });
}

bool BossAI::CheckInRoom()
{
    if (CheckBoundary(me))
        return true;

    EnterEvadeMode();
    return false;
}

void BossAI::_EnterCombat()
{
    if (instance)
    {
        // bosses do not respawn, check only on enter combat
        if (!instance->CheckRequiredBosses(_bossId, me->GetEntry()))
        {
            EnterEvadeMode();
            AddDelayedEvent(200, [=]() -> void
            {
                if (instance)
                    instance->RepopPlayersAtGraveyard();
            });
            return;
        }
        me->setActive(true);
        DoZoneInCombat();
        ScheduleTasks();
        instance->SetBossState(_bossId, IN_PROGRESS);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_ENGAGE, me);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_INSTANCE_START, me);
        instance->SendEncounterUnit(ENCOUNTER_FRAME_SET_ALLOWING_RELEASE, me, false);
    }

    me->SummonCreatureGroup(CREATURE_SUMMON_GROUP_COMBAT);
}

bool BossAI::CheckBoundary(Unit* who)
{
    if (!who)
        who = me;

    if (!GetBoundary())
        return true;

    for (auto const& itr : *GetBoundary())
    {
        switch (itr.first)
        {
        case BOUNDARY_N:
            if (who->GetPositionX() > itr.second)
                return false;
            break;
        case BOUNDARY_S:
            if (who->GetPositionX() < itr.second)
                return false;
            break;
        case BOUNDARY_E:
            if (who->GetPositionY() < itr.second)
                return false;
            break;
        case BOUNDARY_W:
            if (who->GetPositionY() > itr.second)
                return false;
            break;
        case BOUNDARY_NW:
            if (who->GetPositionX() + who->GetPositionY() > itr.second)
                return false;
            break;
        case BOUNDARY_SE:
            if (who->GetPositionX() + who->GetPositionY() < itr.second)
                return false;
            break;
        case BOUNDARY_NE:
            if (who->GetPositionX() - who->GetPositionY() > itr.second)
                return false;
            break;
        case BOUNDARY_SW:
            if (who->GetPositionX() - who->GetPositionY() < itr.second)
                return false;
            break;
        default:
            break;
        }
    }

    return true;
}

void BossAI::JustSummoned(Creature* summon)
{
    std::lock_guard<std::recursive_mutex> guard(summons.m_lock);
    summons.Summon(summon);
    if (me->isInCombat())
        DoZoneInCombat(summon);
}

void BossAI::SummonedCreatureDespawn(Creature* summon)
{
    std::lock_guard<std::recursive_mutex> guard(summons.m_lock);
    summons.Despawn(summon);
}

void BossAI::DoZoneInCombatCheck(uint32 diff)
{
    if (_checkZoneInCombatTimer <= diff)
        _checkZoneInCombatTimer = 5000;
    else
    {
        _checkZoneInCombatTimer -= diff;
        return;
    }

    me->SetInCombatWithZone();
}

void BossAI::UpdateAI(uint32 diff)
{
    if (!UpdateVictim())
        return;

    events.Update(diff);
    DoZoneInCombatCheck(diff);

    if (me->HasUnitState(UNIT_STATE_CASTING))
        return;

    while (uint32 eventId = events.ExecuteEvent())
        ExecuteEvent(eventId);

    DoMeleeAttackIfReady();
}

bool BossAI::_EnterEvadeMode()
{
    if (!me->isAlive())
        return false;

    TC_LOG_DEBUG(LOG_FILTER_NETWORKIO, "BossAI::_EnterEvadeMode %u enters evade mode.", me->GetEntry());

    // dont remove vehicle auras, passengers arent supposed to drop off the vehicle
    me->RemoveAllAurasExceptType(SPELL_AURA_CONTROL_VEHICLE);

    // sometimes bosses stuck in combat?
    me->DeleteThreatList();
    me->ClearSaveThreatTarget();
    me->CombatStop(true);
    me->LoadCreaturesAddon();
    me->SetLootRecipient(nullptr);
    me->ResetPlayerDamageReq();
    me->GetMotionMaster()->MoveTargetedHome();

    return !me->IsInEvadeMode();
}

void BossAI::DoActionSummon(uint32 entry, int32 actionID)
{
    EntryCheckPredicate pred(entry);
    summons.DoAction(actionID, pred);
}

// WorldBossAI - for non-instanced bosses

WorldBossAI::WorldBossAI(Creature* creature) :
    ScriptedAI(creature),
    summons(creature)
{
}

void WorldBossAI::_Reset()
{
    if (!me->isAlive())
        return;

    events.Reset();
    summons.DespawnAll();
}

void WorldBossAI::_JustDied()
{
    events.Reset();
    summons.DespawnAll();
}

void WorldBossAI::_EnterCombat()
{
    Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 0.0f, true);
    if (target)
        AttackStart(target);
}

void WorldBossAI::JustSummoned(Creature* summon)
{
    std::lock_guard<std::recursive_mutex> guard(summons.m_lock);
    summons.Summon(summon);
    Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 0.0f, true);
    if (target)
        summon->AI()->AttackStart(target);
}

void WorldBossAI::SummonedCreatureDespawn(Creature* summon)
{
    std::lock_guard<std::recursive_mutex> guard(summons.m_lock);
    summons.Despawn(summon);
}

void WorldBossAI::UpdateAI(uint32 diff)
{
    if (!UpdateVictim())
        return;

    events.Update(diff);

    if (me->HasUnitState(UNIT_STATE_CASTING))
        return;

    while (uint32 eventId = events.ExecuteEvent())
        ExecuteEvent(eventId);

    DoMeleeAttackIfReady();
}

// BrawlersBossAI - for  brawlers guild

BrawlersBossAI::BrawlersBossAI(Creature* creature) : ScriptedAI(creature), summons(creature)
{
}

void BrawlersBossAI::_Reset()
{
    summons.DespawnAll();
    me->RemoveAllAreaObjects();
}

void BrawlersBossAI::_WinRound()
{
    if (me->GetAnyOwner() && me->GetAnyOwner()->IsPlayer())
    {
        Player* player = me->GetAnyOwner()->ToPlayer();
        player->AddDelayedEvent(700, [player]() -> void
        {
            if (player && player->isAlive())
                if (BrawlersGuild* brawlerGuild = player->GetBrawlerGuild())
                    brawlerGuild->BossReport(player->GetGUID(), true);
        });
    }
}

void BrawlersBossAI::_LoseRound()
{
    if (me->GetAnyOwner() && me->GetAnyOwner()->IsPlayer())
    {
        Player* player = me->GetAnyOwner()->ToPlayer();
        player->AddDelayedEvent(1000, [player]() -> void
        {
            if (player)
                if (BrawlersGuild* brawlerGuild = player->GetBrawlerGuild())
                    brawlerGuild->BossReport(player->GetGUID(), false);
        });
    }
}

void BrawlersBossAI::JustSummoned(Creature* summon)
{
    std::lock_guard<std::recursive_mutex> guard(summons.m_lock);
    summons.Summon(summon);
    if (me->isInCombat())
        DoZoneInCombat(summon);
}

void BrawlersBossAI::KilledUnit(Unit* who)
{
    if (Unit* owner = me->GetAnyOwner())
        if (who->GetGUID() == owner->GetGUID())
        {
            _Reset();
            _LoseRound();
        }
}


// SD2 grid searchers.
Creature* GetClosestCreatureWithEntry(WorldObject* source, uint32 entry, float maxSearchRange, bool alive /*= true*/)
{
    return source->FindNearestCreature(entry, maxSearchRange, alive);
}

GameObject* GetClosestGameObjectWithEntry(WorldObject* source, uint32 entry, float maxSearchRange)
{
    return source->FindNearestGameObject(entry, maxSearchRange);
}

void GetCreatureListWithEntryInGrid(std::list<Creature*>& list, WorldObject* source, uint32 entry, float maxSearchRange)
{
    source->GetCreatureListWithEntryInGrid(list, entry, maxSearchRange);
}

void GetGameObjectListWithEntryInGrid(std::list<GameObject*>& list, WorldObject* source, uint32 entry, float maxSearchRange)
{
    source->GetGameObjectListWithEntryInGrid(list, entry, maxSearchRange);
}

void GetPlayerListInGrid(std::list<Player*>& list, WorldObject* source, float maxSearchRange)
{
    source->GetPlayerListInGrid(list, maxSearchRange);
}

void GetPositionWithDistInOrientation(Unit* pUnit, float dist, float orientation, float& x, float& y)
{
    x = pUnit->GetPositionX() + (dist * cos(orientation));
    y = pUnit->GetPositionY() + (dist * sin(orientation));
}

void GetPosInRadiusWithRandomOrientation(Unit* unit, float dist, float &x, float &y)
{
    float mod = urand(0, 6);
    float orientation = mod <= 5 ? mod + float(urand(1, 9)) / 10 : mod;
    x = unit->GetPositionX() + (dist * cos(orientation));
    y = unit->GetPositionY() + (dist * sin(orientation));
}

void GetRandPosFromCenterInDist(float centerX, float centerY, float dist, float& x, float& y)
{
    float randOrientation = frand(0, 2 * M_PI);

    x = centerX + (dist * cos(randOrientation));
    y = centerY + (dist * sin(randOrientation));
}
