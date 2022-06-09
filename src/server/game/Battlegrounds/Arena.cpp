/*
 * Copyright (C) 2008-2015 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Arena.h"
#include "Bracket.h"
#include "Group.h"
#include "GuildMgr.h"
#include "ArenaScore.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Packets/WorldStatePackets.h"
#include "BattlegroundMgr.h"
#include "Battleground.h"
#include "PlayerBotMgr.h"
#include "BotAI.h"

Arena::Arena()
{
    for (uint8 i = BG_STARTING_EVENT_FIRST; i < BG_STARTING_EVENT_COUNT; ++i)
        m_broadcastMessages[i] = ArenaBroadcastTexts[i];

    _dampeningTimer.SetInterval(Seconds(3).count());
    _winConditionCheckTimer.SetInterval(Seconds(3).count());
    _logData = {};
}

void Arena::Update(uint32 diff)
{
    m_UpdateTick += diff;
    if (m_UpdateTick < 1000)
    {
        Battleground::Update(diff);
        return;
    }
    m_UpdateTick = 0;

    BattlegroundStatus bgState = GetStatus();
    if (bgState == STATUS_NONE || bgState == STATUS_WAIT_QUEUE)
    {
        m_LastCommandTeam = TEAM_NEUTRAL;
        Battleground::Update(diff);
        return;
    }

    int32 needCommandTeam = 0;
    std::list<Player*> alliancePlayers;
    std::list<Player*> hordePlayers;
    for (auto itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
    {
        if (Player* player = ObjectAccessor::FindPlayer(itr->first))
        {
            if (!player->isAlive())
                continue;
            if (player->GetTeamId() == TEAM_ALLIANCE)
                alliancePlayers.push_back(player);
            else if (player->GetTeamId() == TEAM_HORDE)
                hordePlayers.push_back(player);
            bool isPlayerBot = player->IsPlayerBot();
            if (needCommandTeam >= 0)
            {
                if (player->GetTeamId() == TEAM_ALLIANCE)
                {
                    if (!isPlayerBot)
                    {
                        if (needCommandTeam == 1)
                        {
                            needCommandTeam = -1;
                            m_LastCommandTeam = TEAM_NEUTRAL;
                        }
                        else
                        {
                            needCommandTeam = 2;
                            m_LastCommandTeam = TEAM_HORDE;
                        }
                    }
                }
                else if (player->GetTeamId() == TEAM_HORDE)
                {
                    if (!isPlayerBot)
                    {
                        if (needCommandTeam == 2)
                        {
                            needCommandTeam = -1;
                            m_LastCommandTeam = TEAM_NEUTRAL;
                        }
                        else
                        {
                            needCommandTeam = 1;
                            m_LastCommandTeam = TEAM_ALLIANCE;
                        }
                    }
                }
            }

            if (!isPlayerBot || player->GetBattleground() != this)
                continue;
            if (BotBGAI* pBotAI = dynamic_cast<BotBGAI*>(player->GetAI()))
            {
                switch (bgState)
                {
                    case BattlegroundStatus::STATUS_WAIT_JOIN:
                        pBotAI->ReadyBattleground();
                        m_StartTryMount = false;
                        break;
                    case BattlegroundStatus::STATUS_IN_PROGRESS:
                        pBotAI->StartBattleground();
                        break;
                    case BattlegroundStatus::STATUS_WAIT_LEAVE:
                        pBotAI->LeaveBattleground();
                        m_StartTryMount = false;
                        break;
                }
            }
        }
    }
    if (bgState == BattlegroundStatus::STATUS_IN_PROGRESS)
    {
        if (needCommandTeam > 0 && needCommandTeam < 3)
            CommandCombat((needCommandTeam == 1) ? TEAM_ALLIANCE : TEAM_HORDE, alliancePlayers, hordePlayers);
        else if (m_LastCommandTeam != TEAM_NEUTRAL)
            CommandCombat(m_LastCommandTeam, alliancePlayers, hordePlayers);
    }

    Battleground::Update(diff);
}

void Arena::PostUpdateImpl(uint32 diff)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    _dampeningTimer.Update(diff);
    _winConditionCheckTimer.Update(diff);

    if (_dampeningTimer.OnTimerPassReset())
        ApplyDampeningIfNeeded();

    Milliseconds elapsedTime = GetElapsedTime();
    if (elapsedTime >= Minutes(25))
    {
        UpdateArenaWorldState();
        CheckWinConditions();
        return;
    }

    if (_winConditionCheckTimer.OnTimerPassReset())
        CheckWinConditions();

    if (elapsedTime > Minutes(2))
        UpdateWorldState(ARENA_END_TIMER, int32(time(nullptr) + std::chrono::duration_cast<Seconds>(Minutes(25) - elapsedTime).count()));

    ModifyStartDelayTime(Milliseconds(diff));
}

void Arena::_ProcessJoin(uint32 diff)
{
    Battleground::_ProcessJoin(diff);

    if (GetStartDelayTime() <= m_messageTimer[BG_STARTING_EVENT_FOURTH] && !(m_Events & BG_STARTING_EVENT_4))
    {
        uint8 bgQueueTypeId = MS::Battlegrounds::GetBgQueueTypeIdByBgTypeID(GetTypeID(), GetJoinType());

        for (auto const& itr : GetPlayers())
            if (Player* player = ObjectAccessor::FindPlayer(itr.first))
            {
                WorldPackets::Battleground::BattlefieldStatusActive battlefieldStatus;
                sBattlegroundMgr->BuildBattlegroundStatusActive(&battlefieldStatus, this, player, player->GetBattlegroundQueueIndex(bgQueueTypeId), player->GetBattlegroundQueueJoinTime(bgQueueTypeId), GetJoinType());
                player->SendDirectMessage(battlefieldStatus.Write());

                player->RemoveAurasDueToSpell(SPELL_ARENA_PREPARATION);
                player->ResetAllPowers();

                player->RemoveAppliedAuras([](AuraApplicationPtr const aurApp)
                {
                    Aura* aura = aurApp->GetBase();
                    return !aura->IsPermanent() && aura->GetDuration() <= 30 * IN_MILLISECONDS && aurApp->IsPositive() && (!(aura->GetSpellInfo()->HasAttribute(SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY))) && (!aura->HasEffectType(SPELL_AURA_MOD_INVISIBILITY));
                });
            }

        CheckWinConditions();
    }
}

void Arena::AddPlayer(Player* player)
{
    Battleground::AddPlayer(player);

    PlayerScores[player->GetGUID()] = new ArenaScore(player->GetGUID(), player->GetBGTeamId());

    player->ResummonPetTemporaryUnSummonedIfAny();

    if (Pet* pet = player->GetPet())
    {
        if (!pet->isAlive())
            pet->setDeathState(ALIVE);

        pet->SetHealth(pet->GetMaxHealth());
        pet->RemoveAllAuras();

        if (player->HasSpell(155228) || player->HasSpell(205024) || player->GetSpecializationId() == SPEC_MAGE_FIRE &&
            player->GetSpecializationId() == SPEC_MAGE_ARCANE || player->GetSpecializationId() == SPEC_DK_BLOOD || player->GetSpecializationId() == SPEC_DK_FROST)
            player->RemovePet(pet);
    }

    player->RemoveArenaEnchantments(TEMP_ENCHANTMENT_SLOT);

    uint32 team = player->GetTeam();
    if (team == ALLIANCE)
        player->CastSpell(player, player->GetBGTeam() == HORDE ? SPELL_BG_ALLIANCE_GREEN_FLAG : SPELL_BG_ALLIANCE_GOLD_FLAG, true);
    else
        player->CastSpell(player, player->GetBGTeam() == HORDE ? SPELL_BG_HORDE_GREEN_FLAG : SPELL_BG_HORDE_GOLD_FLAG, true);

    player->DestroyConjuredItems(true);
    player->UnsummonPetTemporaryIfAny();

    if (GetStatus() == STATUS_WAIT_JOIN)
    {
        player->CastSpell(player, SPELL_ARENA_PREPARATION, true);
        player->CastSpell(player, SPELL_ARENA_PERIODIC_AURA, true);
        player->CastSpell(player, SPELL_ENTERING_BATTLEGROUND, true);
        if (IsRated())
            player->CastSpell(player, SPELL_RATED_PVP_TRANSFORM_SUPPRESSION, true);

        player->ResetAllPowers();
    }

    if (!player->IsSpectator())
    {
        SendOpponentSpecialization(team);
        SendOpponentSpecialization(GetOtherTeam(team));
    }

    UpdateArenaWorldState();
}

void Arena::RemovePlayer(Player* player, ObjectGuid /*guid*/, uint32 /*team*/)
{
    BotUtility::RemoveArenaBotSpellsByPlayer(player);
    if (GetStatus() == STATUS_WAIT_LEAVE)
        return;

    UpdateArenaWorldState();
    CheckWinConditions();
}

void Arena::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(ARENA_SHOW_END_TIMER, GetStatus() == STATUS_IN_PROGRESS);
    packet.Worldstates.emplace_back(ARENA_END_TIMER, int32(time(nullptr) + std::chrono::duration_cast<Seconds>(Minutes(25) - GetElapsedTime()).count()));
    packet.Worldstates.emplace_back(ARENA_ALIVE_PLAYERS_GREEN, GetAlivePlayersCountByTeam(HORDE));
    packet.Worldstates.emplace_back(ARENA_ALIVE_PLAYERS_GOLD, GetAlivePlayersCountByTeam(ALLIANCE));
    packet.Worldstates.emplace_back(BG_RV_WORLD_STATE, 1);
}

void Arena::UpdateArenaWorldState()
{
    UpdateWorldState(ARENA_ALIVE_PLAYERS_GREEN, GetAlivePlayersCountByTeam(HORDE));
    UpdateWorldState(ARENA_ALIVE_PLAYERS_GOLD, GetAlivePlayersCountByTeam(ALLIANCE));
}

void Arena::HandleKillPlayer(Player* player, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    Battleground::HandleKillPlayer(player, killer);

    UpdateArenaWorldState();
    CheckWinConditions();
}

void Arena::StartingEventCloseDoors()
{
    UpdateWorldState(ARENA_SHOW_END_TIMER, 0);
    Battleground::StartingEventCloseDoors();
}

void Arena::StartingEventOpenDoors()
{
    UpdateWorldState(ARENA_SHOW_END_TIMER, 1);

    CheckWinConditions();

    for (auto const& v : GetPlayers())
        if (Player* player = ObjectAccessor::FindPlayer(v.first))
        {
            player->RemoveAurasDueToSpell(SPELL_ARENA_PREPARATION);
            player->ResetAllPowers();

            player->RemoveAppliedAuras([](AuraApplicationPtr const aurApp)
            {
                Aura* aura = aurApp->GetBase();
                return !aura->IsPermanent() && aura->GetDuration() <= 30 * IN_MILLISECONDS && aurApp->IsPositive() && (!(aura->GetSpellInfo()->HasAttribute(SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY))) && (!aura->HasEffectType(SPELL_AURA_MOD_INVISIBILITY));
            });
        }

    Battleground::StartingEventOpenDoors();

    _logData = {};
    _logData.RealmID = realm.Id.Realm;
    _logData.MapID = GetMapId();
    _logData.Arena = boost::in_place();
    _logData.Arena->JoinType = GetJoinType();

    for (const auto& itr : GetPlayers())
    {
        if (auto player = ObjectAccessor::FindPlayer(itr.first))
        {
            if (auto group = player->GetGroup())
            {
                if (!player->GetGuild() || !group->IsGuildGroup())
                    continue;

                _logData.Guild = boost::in_place();
                _logData.Guild->GuildID = player->GetGuildId();
                _logData.Guild->GuildFaction = player->GetTeamId();
                _logData.Guild->GuildName = player->GetGuildName();
                break;
            }
        }
    }
}

void Arena::RemovePlayerAtLeave(ObjectGuid guid, bool transport, bool sendPacket)
{
    if (Player* player = ObjectAccessor::FindPlayer(guid))
    {
        player->RemoveAurasDueToSpell(SPELL_ARENA_PERIODIC_AURA);
        player->RemoveAurasDueToSpell(SPELL_ENTERING_BATTLEGROUND);
        if (IsRated())
            player->RemoveAurasDueToSpell(SPELL_RATED_PVP_TRANSFORM_SUPPRESSION);

        BotUtility::RemoveArenaBotSpellsByPlayer(player);
    }

    Battleground::RemovePlayerAtLeave(guid, transport, sendPacket);
}

void Arena::CheckWinConditions()
{
    if (!GetAlivePlayersCountByTeam(ALLIANCE) && GetPlayersCountByTeam(HORDE))
        EndBattleground(HORDE);
    else if (GetPlayersCountByTeam(ALLIANCE) && !GetAlivePlayersCountByTeam(HORDE))
        EndBattleground(ALLIANCE);

    if (GetElapsedTime() >= Minutes(25))
    {
        if (GetAlivePlayersCountByTeam(ALLIANCE) < GetPlayersCountByTeam(HORDE))
            EndBattleground(HORDE);
        else if (GetPlayersCountByTeam(ALLIANCE) > GetAlivePlayersCountByTeam(HORDE))
            EndBattleground(ALLIANCE);
        else
            EndBattleground(WINNER_NONE);
    }
}

void Arena::ApplyDampeningIfNeeded()
{
    auto applyDampening([=]() -> void
    {
        for (auto const& itr : GetPlayers())
            if (auto const& player = GetPlayer(itr, "ApplyDampeningIfNeeded"))
                if (!player->HasAura(SPELL_BG_ARENA_DUMPENING))
                    player->AddAura(SPELL_BG_ARENA_DUMPENING, player);
    });

    uint8 joinType = GetJoinType();
    if (GetBrawlJoinType())
        joinType = GetBrawlJoinType();

    switch (joinType)
    {
        case MS::Battlegrounds::JoinType::Arena2v2:
            applyDampening();
            break;
        case MS::Battlegrounds::JoinType::ArenaSoloQ3v3:
        case MS::Battlegrounds::JoinType::Arena3v3:
            if (GetElapsedTime() >= std::chrono::minutes(5))
                applyDampening();
            break;
        default:
            break;
    }
}

void Arena::EndBattleground(uint32 winner)
{
    if (IsRated())
    {
        _logData.Arena->WinnerTeamId = winner;
        _logData.Arena->Duration = GetElapsedTime().count();
        _logData.Arena->WinnerOldRating = GetMatchmakerRating(winner);
        _logData.Arena->WinnerNewRating = 0;
        _logData.Arena->LooserOldRating = GetMatchmakerRating(GetOtherTeam(winner));
        _logData.Arena->LooserNewRating = 0; // _arenaTeamScores[MS::Battlegrounds::GetTeamIdByTeam(winner)].NewRating;

        for (const auto& itr : GetPlayers())
        {
            if (auto player = ObjectAccessor::FindPlayer(itr.first))
            {
                LogsSystem::RosterData data;
                data.GuidLow = player->GetGUIDLow();
                data.Name = player->GetName();
                data.Level = player->getLevel();
                data.Class = player->getClass();
                data.SpecID = player->GetSpecializationId();
                data.Role = player->GetSpecializationRole();
                data.ItemLevel = player->GetAverageItemLevelEquipped();
                data.TeamId = player->GetBGTeamId();
                _logData.Rosters.push_back(data);
            }
        }

        sLog->OutPveEncounter(_logData.Serealize().c_str());
        _logData = {};
    }

    if (IsRated() && winner != WINNER_NONE && GetStatus() == STATUS_IN_PROGRESS && GetJoinType() != MS::Battlegrounds::JoinType::Arena1v1)
    {
        std::string winnerTeam;
        std::string loserTeam;
        bool attention = HandleArenaLogPlayerNames(winnerTeam, loserTeam, "!?$", winner);
        uint32 _arenaTimer = GetElapsedTime().count();
        uint32 _min = _arenaTimer / IN_MILLISECONDS / 60;
        uint32 _sec = (_arenaTimer - _min * 60 * IN_MILLISECONDS) / IN_MILLISECONDS;
        std::string att;

        switch (GetJoinType())
        {
            case MS::Battlegrounds::JoinType::Arena1v1:
            {
                if (attention || GetElapsedTime() < Seconds(50))
                    att += "--- ATTENTION!";

                // sLog->outArena("FINISH: Arena match Type: 1v1 --- Winner[%s]: old rating: %u --- Loser[%s]: old rating: %u --- Duration: %u min. %u sec. %s",
                //              winnerTeam.c_str(), GetMatchmakerRating(winner), loserTeam.c_str(), GetMatchmakerRating(GetOtherTeam(winner)), _min, _sec, att.c_str());
                break;
            }
            case MS::Battlegrounds::JoinType::Arena2v2:
            {
                if (attention || GetElapsedTime() < Seconds(70))
                    att += "--- ATTENTION!";

                sLog->outArena(MS::Battlegrounds::JoinType::Arena2v2, "FINISH: Arena match Type: 2v2 --- Winner[%s]: old rating: %u --- Loser[%s]: old rating: %u --- Duration: %u min. %u sec. %s",
                               winnerTeam.c_str(), GetMatchmakerRating(winner), loserTeam.c_str(), GetMatchmakerRating(GetOtherTeam(winner)), _min, _sec, att.c_str());
                break;
            }
            case MS::Battlegrounds::JoinType::Arena3v3:
            {
                if (attention || GetElapsedTime() < Seconds(100))
                    att += "--- ATTENTION!";

                sLog->outArena(MS::Battlegrounds::JoinType::Arena3v3, "FINISH: Arena match Type: 3v3 --- Winner[%s]: old rating: %u --- Loser[%s]: old rating: %u --- Duration: %u min. %u sec. %s",
                               winnerTeam.c_str(), GetMatchmakerRating(winner), loserTeam.c_str(), GetMatchmakerRating(GetOtherTeam(winner)), _min, _sec, att.c_str());
                break;
            }
            default:
                sLog->outArena(GetJoinType(), "match Type: %u --- Winner: old rating: %u  --- Loser: old rating: %u| DETAIL: Winner[%s] Loser[%s]",
                    GetJoinType(), GetMatchmakerRating(winner), GetMatchmakerRating(GetOtherTeam(winner)), winnerTeam.c_str(), loserTeam.c_str());
                break;
        }
    }

    Battleground::EndBattleground(winner);
}

void Arena::SendOpponentSpecialization(uint32 team)
{
    WorldPackets::Battleground::ArenaPrepOpponentSpecializations spec;

    for (auto const& itr : GetPlayers())
        if (auto const& opponent = GetPlayer(itr, "SendOponentSpecialization"))
        {
            if (itr.second.Team != team)
                continue;

            WorldPackets::Battleground::ArenaPrepOpponentSpecializations::OpponentSpecData data;
            data.Guid = opponent->GetGUID();
            data.SpecializationID = opponent->GetUInt32Value(PLAYER_FIELD_CURRENT_SPEC_ID);
            spec.Data.emplace_back(data);
        }

    SendPacketToTeam(GetOtherTeam(team), spec.Write());
}

void Arena::CommandCombat(TeamId commandTeam, PLAYERS& alliances, PLAYERS& hordes)
{
    if (commandTeam == TEAM_NEUTRAL)
        return;
    if (alliances.empty() || hordes.empty())
        return;
    PLAYERS& comLists = (commandTeam == TEAM_ALLIANCE) ? alliances : hordes;
    PLAYERS& enemyLists = (commandTeam == TEAM_ALLIANCE) ? hordes : alliances;
    if (!m_StartTryMount)
    {
        m_StartTryMount = true;
        if (IsRated())
        {
            for (Player* self : comLists)
            {
                BotUtility::AddArenaBotSpellsByPlayer(self);
            }
        }
        for (Player* player : alliances)
        {
            if (BotBGAI* pBotAI = dynamic_cast<BotBGAI*>(player->GetAI()))
                pBotAI->TryUpMount();
        }
        for (Player* player : hordes)
        {
            if (BotBGAI* pBotAI = dynamic_cast<BotBGAI*>(player->GetAI()))
                pBotAI->TryUpMount();
        }
    }

    //PLAYERS canSelectEnemys;
    //for (Player* enemyPlayer : enemyLists)
    //{
    //	if (!CanSelectTarget(enemyPlayer))
    //		continue;
    //	canSelectEnemys.push_back(enemyPlayer);
    //}
    //if (canSelectEnemys.empty())
    //	return;
    if (AssignTactics(comLists, enemyLists))
        return;
    if (ExistMightinessHealer(enemyLists))
    {
        SuppressHealerPlayer(comLists, enemyLists);
        for (Player* enemyPlayer : enemyLists)
        {
            if (!IsMightiness(enemyPlayer))
                continue;
            if (!CanSelectTarget(enemyPlayer))
                continue;
            for (Player* comPlayer : comLists)
                comPlayer->SetSelection(enemyPlayer->GetGUID());
        }
        for (Player* enemyPlayer : enemyLists)
        {
            if (!IsRangeBot(enemyPlayer))
                continue;
            if (!CanSelectTarget(enemyPlayer))
                continue;
            for (Player* comPlayer : comLists)
                comPlayer->SetSelection(enemyPlayer->GetGUID());
            return;
        }
        NormalTactics(comLists, enemyLists);
        return;
    }
    else if (HasFullSuppressSpell(comLists))
    {
        if (ExistMightinessClasses(enemyLists))
        {
            SuppressMightinessPlayer(comLists, enemyLists);
            NormalTactics(comLists, enemyLists);
            return;
        }
        else if (CanFullHealerEnemy2(enemyLists))
        {
            SuppressRealPlayer(comLists, enemyLists);
            NormalTactics(comLists, enemyLists);
            return;
        }
        else if (ExistRealPlayerByRange(enemyLists))
        {
            SuppressHealerPlayer(comLists, enemyLists);
            for (Player* enemyPlayer : enemyLists)
            {
                if (enemyPlayer->IsPlayerBot() || enemyPlayer->IsMounted())
                    continue;
                if (!CanSelectTarget(enemyPlayer))
                    continue;
                for (Player* comPlayer : comLists)
                    comPlayer->SetSelection(enemyPlayer->GetGUID());
                return;
            }
            NormalTactics(comLists, enemyLists);
            return;
        }
        else
        {
            SuppressRealPlayer(comLists, enemyLists);
            NormalTactics(comLists, enemyLists);
            return;
        }
    }
    else if (CanFullHealerEnemy(enemyLists))
    {
        SuppressRealPlayer(comLists, enemyLists);
        NormalTactics(comLists, enemyLists);
        return;
    }
    else if (NeedHarassHealer(comLists, enemyLists))
    {
        for (Player* self : comLists)
        {
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
                pAI->ClearCruxControlCommand();
        }
        FollowEnemyHealer(comLists, enemyLists);
        for (Player* enemyPlayer : enemyLists)
        {
            if (enemyPlayer->IsPlayerBot() || enemyPlayer->IsMounted())
                continue;
            if (!CanSelectTarget(enemyPlayer))
                continue;
            for (Player* comPlayer : comLists)
                comPlayer->SetSelection(enemyPlayer->GetGUID());
            return;
        }
        NormalTactics(comLists, enemyLists);
        return;
    }
    else
    {
        SuppressHealerPlayer(comLists, enemyLists);
        for (Player* enemyPlayer : enemyLists)
        {
            if (enemyPlayer->IsPlayerBot() || enemyPlayer->IsMounted())
                continue;
            if (!CanSelectTarget(enemyPlayer))
                continue;
            for (Player* comPlayer : comLists)
                comPlayer->SetSelection(enemyPlayer->GetGUID());
            return;
        }
    }

    for (Player* enemyPlayer : enemyLists)
    {
        if (!IsHealerBot(enemyPlayer))
            continue;
        if (!CanSelectTarget(enemyPlayer))
            continue;
        for (Player* comPlayer : comLists)
            comPlayer->SetSelection(enemyPlayer->GetGUID());
        return;
    }
    for (Player* enemyPlayer : enemyLists)
    {
        if (!IsRangeBot(enemyPlayer))
            continue;
        if (!CanSelectTarget(enemyPlayer))
            continue;
        for (Player* comPlayer : comLists)
            comPlayer->SetSelection(enemyPlayer->GetGUID());
        return;
    }
    for (Player* enemyPlayer : enemyLists)
    {
        if (!IsMeleeBot(enemyPlayer))
            continue;
        if (!CanSelectTarget(enemyPlayer))
            continue;
        for (Player* comPlayer : comLists)
            comPlayer->SetSelection(enemyPlayer->GetGUID());
        return;
    }
    for (Player* enemyPlayer : enemyLists)
    {
        if (!CanSelectTarget(enemyPlayer))
            continue;
        for (Player* comPlayer : comLists)
            comPlayer->SetSelection(enemyPlayer->GetGUID());
        return;
    }
}

bool Arena::NeedHarassHealer(PLAYERS& selfPlayer, PLAYERS& enemyPlayer)
{
    uint32 existMelee = 0;
    for (Player* self : selfPlayer)
    {
        if (IsAllMeleeBot(self))
        {
            ++existMelee;
        }
    }
    if (existMelee < 1)
        return false;
    if (CanFullSuppressPlayer(enemyPlayer))
        return false;

    return true;
}

bool Arena::HasFullSuppressSpell(PLAYERS& selfPlayer)
{
    std::set<uint32> fullCtrlers;
    std::set<uint32> onceCtrlers;
    for (Player* self : selfPlayer)
    {
        Classes cls = Classes(self->getClass());
        switch (cls)
        {
            case CLASS_WARRIOR:
            case CLASS_DEATH_KNIGHT:
            case CLASS_PRIEST:
                break;
            case CLASS_PALADIN:
                if (self->FindTalentType() == 2)
                {
                    if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
                        onceCtrlers.insert(pAI->GetControlSpellDiminishingGroup());
                }
                break;
            case CLASS_ROGUE:
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
                if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
                    onceCtrlers.insert(pAI->GetControlSpellDiminishingGroup());
                break;
            case CLASS_MAGE:
            case CLASS_WARLOCK:
            case CLASS_DRUID:
                if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
                    fullCtrlers.insert(pAI->GetControlSpellDiminishingGroup());
                break;
        }
    }
    if (fullCtrlers.size() >= 2)
    {
        for (Player* self : selfPlayer)
        {
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
                pAI->SetReserveCtrlSpell(true);
        }
        return true;
    }
    if (fullCtrlers.size() >= 1 && onceCtrlers.size() >= 2)
    {
        for (Player* self : selfPlayer)
        {
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
                pAI->SetReserveCtrlSpell(true);
        }
        return true;
    }

    for (Player* self : selfPlayer)
    {
        if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
        {
            pAI->SetReserveCtrlSpell(false);
            pAI->ClearCruxControlCommand();
        }
    }
    return false;
}

bool Arena::CanFullSuppressPlayer(PLAYERS& enemyPlayer)
{
    for (Player* enemy : enemyPlayer)
    {
        if (enemy->IsPlayerBot())
            continue;
        Classes cls = Classes(enemy->getClass());
        switch (cls)
        {
            case CLASS_WARRIOR:
            case CLASS_PALADIN:
            case CLASS_ROGUE:
            case CLASS_DEATH_KNIGHT:
                return false;
            case CLASS_MAGE:
            case CLASS_WARLOCK:
            case CLASS_PRIEST:
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
                return true;
            case CLASS_DRUID:
                return (enemy->FindTalentType() != 1);
        }
        break;
    }
    return false;
}

bool Arena::SuppressRealPlayer(PLAYERS& selfPlayer, PLAYERS& enemyPlayer)
{
    if (enemyPlayer.size() < 2 || !ExistControler(selfPlayer))
    {
        for (Player* self : selfPlayer)
        {
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
                pAI->ClearCruxControlCommand();
        }
        return false;
    }
    for (PLAYERS::iterator itEnemy = enemyPlayer.begin();
        itEnemy != enemyPlayer.end();
        itEnemy++)
    {
        if ((*itEnemy)->IsPlayerBot())// || IsRangeBot(*itEnemy))
            continue;
        for (Player* sets : selfPlayer)
        {
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(sets->GetAI()))
            {
                pAI->SetReserveCtrlSpell(true);
                pAI->SetNonSelectTarget((*itEnemy)->GetGUID());
            }
        }
        if (MeleeTargetIsSuppress(*itEnemy))
        {
            enemyPlayer.erase(itEnemy);
            return true;
        }
        if (ExistCtrlSpellCasting(selfPlayer))
        {
            enemyPlayer.erase(itEnemy);
            return true;
        }
        bool canRemove = false;
        for (PLAYERS::iterator itSelf = selfPlayer.begin();
            itSelf != selfPlayer.end();
            itSelf++)
        {
            if (TargetIsOverSuppress(*itSelf))
                continue;
            if ((*itSelf)->HasUnitState(UNIT_STATE_CASTING))
            {
                canRemove = true;
                continue;
            }
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>((*itSelf)->GetAI()))
            {
                float dist = pAI->TryPushControlCommand((*itEnemy));
                if (dist >= 0)
                {
                    //selfPlayer.erase(itSelf);
                    enemyPlayer.erase(itEnemy);
                    return true;
                }
            }
        }
        if (canRemove)
            enemyPlayer.erase(itEnemy);
        return false;
    }
    for (Player* self : selfPlayer)
    {
        if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
        {
            pAI->SetReserveCtrlSpell(false);
            pAI->ClearCruxControlCommand();
        }
    }
    return false;
}

bool Arena::SuppressHealerPlayer(PLAYERS& selfPlayer, PLAYERS& enemyPlayer)
{
    if (enemyPlayer.size() < 2 || !ExistControler(selfPlayer))
    {
        for (Player* self : selfPlayer)
        {
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
                pAI->ClearCruxControlCommand();
        }
        return false;
    }
    for (PLAYERS::iterator itEnemy = enemyPlayer.begin();
        itEnemy != enemyPlayer.end();
        itEnemy++)
    {
        Player* targetEnemy = *itEnemy;
        if (!IsHealerBot(targetEnemy))
            continue;
        float manaPct = uint32(((float)targetEnemy->GetPower(POWER_MANA) / (float)targetEnemy->GetMaxPower(POWER_MANA)) * 100);
        if (manaPct <= 5 || targetEnemy->GetHealthPct() < 10)
            continue;
        for (Player* sets : selfPlayer)
        {
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(sets->GetAI()))
            {
                pAI->SetReserveCtrlSpell(true);
                pAI->SetNonSelectTarget((*itEnemy)->GetGUID());
            }
        }
        if (TargetIsOverSuppress(targetEnemy))
        {
            enemyPlayer.erase(itEnemy);
            return true;
        }
        if (ExistCtrlSpellCasting(selfPlayer))
        {
            enemyPlayer.erase(itEnemy);
            return true;
        }
        //std::map<float, PLAYERS::iterator> canUsePlayers;
        bool canRemove = false;
        for (PLAYERS::iterator itSelf = selfPlayer.begin();
            itSelf != selfPlayer.end();
            itSelf++)
        {
            if (TargetIsOverSuppress(*itSelf))
                continue;
            if ((*itSelf)->HasUnitState(UNIT_STATE_CASTING))
            {
                canRemove = true;
                continue;
            }
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>((*itSelf)->GetAI()))
            {
                float dist = pAI->TryPushControlCommand(targetEnemy);
                if (dist >= 0)
                {
                    //canUsePlayers[dist] = itSelf;
                    //selfPlayer.erase(itSelf);
                    enemyPlayer.erase(itEnemy);
                    return true;
                }
            }
        }
        if (canRemove)
            enemyPlayer.erase(itEnemy);
        return false;
    }
    //if (canUsePlayers.size() < 2 || needSuppPlayer == enemyPlayer.end())
    for (Player* self : selfPlayer)
    {
        if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
        {
            pAI->SetReserveCtrlSpell(false);
            pAI->ClearCruxControlCommand();
        }
    }
    return false;
}

bool Arena::SuppressMightinessPlayer(PLAYERS& selfPlayer, PLAYERS& enemyPlayer)
{
    if (enemyPlayer.size() < 2 || !ExistControler(selfPlayer))
    {
        for (Player* self : selfPlayer)
        {
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
                pAI->ClearCruxControlCommand();
        }
        return false;
    }
    for (PLAYERS::iterator itEnemy = enemyPlayer.begin();
        itEnemy != enemyPlayer.end();
        itEnemy++)
    {
        Player* targetEnemy = *itEnemy;
        if (targetEnemy->IsPlayerBot() || !IsMightiness(targetEnemy))
            continue;
        for (Player* sets : selfPlayer)
        {
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(sets->GetAI()))
            {
                pAI->SetReserveCtrlSpell(true);
                pAI->SetNonSelectTarget(targetEnemy->GetGUID());
            }
        }
        if (TargetIsOverSuppress(targetEnemy))
        {
            enemyPlayer.erase(itEnemy);
            return true;
        }
        if (ExistCtrlSpellCasting(selfPlayer))
        {
            enemyPlayer.erase(itEnemy);
            return true;
        }
        //std::map<float, PLAYERS::iterator> canUsePlayers;
        bool canRemove = false;
        for (PLAYERS::iterator itSelf = selfPlayer.begin();
            itSelf != selfPlayer.end();
            itSelf++)
        {
            if (TargetIsOverSuppress(*itSelf))
                continue;
            if ((*itSelf)->HasUnitState(UNIT_STATE_CASTING))
            {
                canRemove = true;
                continue;
            }
            if (BotBGAI* pAI = dynamic_cast<BotBGAI*>((*itSelf)->GetAI()))
            {
                float dist = pAI->TryPushControlCommand(targetEnemy);
                if (dist >= 0)
                {
                    //canUsePlayers[dist] = itSelf;
                    //selfPlayer.erase(itSelf);
                    enemyPlayer.erase(itEnemy);
                    return true;
                }
            }
        }
        if (canRemove)
            enemyPlayer.erase(itEnemy);
        return false;
    }
    //if (canUsePlayers.size() < 2 || needSuppPlayer == enemyPlayer.end())
    for (Player* self : selfPlayer)
    {
        if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(self->GetAI()))
        {
            pAI->SetReserveCtrlSpell(false);
            pAI->ClearCruxControlCommand();
        }
    }
    return false;
}

void Arena::FollowEnemyHealer(PLAYERS& selfPlayer, PLAYERS& enemyPlayer)
{
    while (true)
    {
        PLAYERS::iterator& itHealer = GetListHealer(enemyPlayer);
        if (itHealer == enemyPlayer.end())
            return;
        PLAYERS::iterator& itMeleer = GetListMeleer(selfPlayer);
        if (itMeleer == selfPlayer.end())
        {
            itMeleer = GetListRanger(selfPlayer);
            if (itMeleer == selfPlayer.end())
                return;
        }
        (*itMeleer)->SetSelection((*itHealer)->GetGUID());
        selfPlayer.erase(itMeleer);
        enemyPlayer.erase(itHealer);
        itHealer = GetListHealer(enemyPlayer);
    }
}

Arena::PLAYERS::iterator Arena::GetListHealer(PLAYERS& players)
{
    for (PLAYERS::iterator itPlayer = players.begin();
        itPlayer != players.end();
        itPlayer++)
    {
        if (!IsHealerBot(*itPlayer))
            continue;
        return itPlayer;
    }
    return players.end();
}

Arena::PLAYERS::iterator Arena::GetListMeleer(PLAYERS& players)
{
    for (PLAYERS::iterator itPlayer = players.begin();
        itPlayer != players.end();
        itPlayer++)
    {
        if (!IsAllMeleeBot(*itPlayer))
            continue;
        return itPlayer;
    }
    return players.end();
}

Arena::PLAYERS::iterator Arena::GetListRanger(PLAYERS& players)
{
    for (PLAYERS::iterator itPlayer = players.begin();
        itPlayer != players.end();
        itPlayer++)
    {
        if (!IsRangeBot(*itPlayer))
            continue;
        return itPlayer;
    }
    return players.end();
}

bool Arena::HasAuraMechanic(Unit* pTarget, Mechanics mask)
{
    if (!pTarget)
        return false;
    return (pTarget->HasAuraWithMechanic(1 << mask));
}

bool Arena::CanSelectTarget(Player* pTarget)
{
    //if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_IMMUNE_SHIELD))
    //    return false;
    if (pTarget->HasAura(27827)) // (27827 救赎之魂 神牧死亡后)
        return false;
    if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_CHARM))
        return false;
    if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_SLEEP))
        return false;
    //if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_KNOCKOUT))
    //    return false;
    if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_POLYMORPH))
        return false;
    if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_BANISH))
        return false;
    if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_HORROR))
        return false;
    if (pTarget->HasAura(1784) || pTarget->HasAura(5215) || pTarget->HasAura(66)) // (1784 盗贼潜行 || 5215 德鲁伊潜行 || 66 法师隐形)
        return false;

    return true;
}

bool Arena::MeleeTargetIsSuppress(Player* pTarget)
{
    if (!pTarget)
        return false;
    if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_CHARM) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_DISORIENTED) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_DISTRACT) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_SLEEP) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_POLYMORPH) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_ROOT) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_STUN) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_BANISH) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_FEAR)
        //|| HasAuraMechanic(pTarget, Mechanics::MECHANIC_IMMUNE_SHIELD)
        )
        return true;
    return false;
}

bool Arena::TargetIsOverSuppress(Player* pTarget)
{
    if (!pTarget)
        return false;
    if (HasAuraMechanic(pTarget, Mechanics::MECHANIC_CHARM) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_DISORIENTED) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_DISTRACT) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_SLEEP) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_POLYMORPH) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_STUN) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_BANISH) ||
        HasAuraMechanic(pTarget, Mechanics::MECHANIC_FEAR)
        //|| HasAuraMechanic(pTarget, Mechanics::MECHANIC_MAGICAL_IMMUNITY)
        )
        return true;
    return false;
}

bool Arena::IsHealerBot(Player* pTarget)
{
    Classes cls = Classes(pTarget->getClass());
    switch (cls)
    {
        case CLASS_WARRIOR:
        case CLASS_HUNTER:
        case CLASS_ROGUE:
        case CLASS_DEATH_KNIGHT:
        case CLASS_MAGE:
        case CLASS_WARLOCK:
            return false;
        case CLASS_PALADIN:
            return (pTarget->FindTalentType() == 0);
        case CLASS_PRIEST:
            return (pTarget->FindTalentType() != 2);
        case CLASS_SHAMAN:
            return (pTarget->FindTalentType() == 2);
        case CLASS_DRUID:
            return (pTarget->FindTalentType() == 2);
    }

    return false;
}

bool Arena::IsMeleeBot(Player* pTarget)
{
    Classes cls = Classes(pTarget->getClass());
    switch (cls)
    {
        case CLASS_WARRIOR:
            return (pTarget->FindTalentType() != 2);
        case CLASS_PALADIN:
            return (pTarget->FindTalentType() != 1);
        case CLASS_ROGUE:
        case CLASS_DEATH_KNIGHT:
            return true;
        case CLASS_MAGE:
        case CLASS_WARLOCK:
        case CLASS_PRIEST:
        case CLASS_HUNTER:
            return false;
        case CLASS_SHAMAN:
            return (pTarget->FindTalentType() == 1);
        case CLASS_DRUID:
            return (pTarget->FindTalentType() == 1);
    }

    return false;
}

bool Arena::IsAllMeleeBot(Player* pTarget)
{
    Classes cls = Classes(pTarget->getClass());
    switch (cls)
    {
        case CLASS_PALADIN:
            return (pTarget->FindTalentType() == 2);
        case CLASS_WARRIOR:
        case CLASS_ROGUE:
        case CLASS_DEATH_KNIGHT:
            return true;
        case CLASS_MAGE:
        case CLASS_WARLOCK:
        case CLASS_PRIEST:
        case CLASS_HUNTER:
        case CLASS_SHAMAN:
            return false;
        case CLASS_DRUID:
            return (pTarget->FindTalentType() == 1);
    }

    return false;
}

bool Arena::IsRangeBot(Player* pTarget)
{
    Classes cls = Classes(pTarget->getClass());
    switch (cls)
    {
        case CLASS_WARRIOR:
        case CLASS_PALADIN:
        case CLASS_ROGUE:
        case CLASS_DEATH_KNIGHT:
            return false;
        case CLASS_MAGE:
        case CLASS_WARLOCK:
        case CLASS_PRIEST:
        case CLASS_HUNTER:
            return true;
        case CLASS_SHAMAN:
            return (pTarget->FindTalentType() != 1);
        case CLASS_DRUID:
            return (pTarget->FindTalentType() != 1);
    }

    return false;
}

bool Arena::IsMightiness(Player* pTarget)
{
    Classes cls = Classes(pTarget->getClass());
    switch (cls)
    {
        case CLASS_WARRIOR:
            return (pTarget->FindTalentType() != 2);
        case CLASS_PALADIN:
        case CLASS_ROGUE:
        case CLASS_DEATH_KNIGHT:
        case CLASS_MAGE:
        case CLASS_WARLOCK:
        case CLASS_PRIEST:
        case CLASS_HUNTER:
        case CLASS_SHAMAN:
        case CLASS_DRUID:
            return false;
    }

    return false;
}

bool Arena::CanFullHealerEnemy(PLAYERS& players)
{
    if (ExistMightinessClasses(players))
        return false;
    for (Player* player : players)
    {
        if (player->IsPlayerBot())
            continue;
        Classes cls = Classes(player->getClass());
        switch (cls)
        {
            case CLASS_WARRIOR:
                return (player->FindTalentType() == 2);
            case CLASS_ROGUE:
            case CLASS_DEATH_KNIGHT:
            case CLASS_HUNTER:
                return true;
            case CLASS_PALADIN:
                return (player->FindTalentType() != 0);
            case CLASS_SHAMAN:
                return (player->FindTalentType() != 2);
            case CLASS_PRIEST:
                return (player->FindTalentType() == 2);
            case CLASS_DRUID:
                return (player->FindTalentType() != 2);
            case CLASS_MAGE:
            case CLASS_WARLOCK:
                return false;
        }
    }
    return false;
}

bool Arena::CanFullHealerEnemy2(PLAYERS& players)
{
    for (Player* player : players)
    {
        if (player->IsPlayerBot())
            continue;
        Classes cls = Classes(player->getClass());
        switch (cls)
        {
            case CLASS_WARRIOR:
            case CLASS_ROGUE:
            case CLASS_DEATH_KNIGHT:
            case CLASS_HUNTER:
            case CLASS_MAGE:
            case CLASS_WARLOCK:
                return false;
            case CLASS_PALADIN:
                return (player->FindTalentType() == 0);
            case CLASS_SHAMAN:
                return (player->FindTalentType() == 2);
            case CLASS_PRIEST:
                return (player->FindTalentType() != 2);
            case CLASS_DRUID:
                return (player->FindTalentType() == 2);
        }
    }
    return false;
}

bool Arena::ExistCtrlSpellCasting(PLAYERS& players)
{
    for (Player* player : players)
    {
        if (BotBGAI* pAI = dynamic_cast<BotBGAI*>(player->GetAI()))
        {
            if (pAI->CastingControlCommandSpell())
                return true;
        }
    }
    return false;
}

bool Arena::ExistRealPlayerByRange(PLAYERS& players)
{
    for (Player* player : players)
    {
        if (player->IsPlayerBot())
            continue;
        return IsRangeBot(player);
    }
    return false;
}

bool Arena::ExistControler(PLAYERS& players)
{
    for (Player* self : players)
    {
        Classes cls = Classes(self->getClass());
        switch (cls)
        {
            case CLASS_MAGE:
            case CLASS_WARLOCK:
            case CLASS_DRUID:
                return true;
            case CLASS_WARRIOR:
            case CLASS_DEATH_KNIGHT:
            case CLASS_PRIEST:
            case CLASS_ROGUE:
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
            case CLASS_PALADIN:
                break;
        }
    }
    return false;
}

bool Arena::ExistMightinessClasses(PLAYERS& players)
{
    for (Player* player : players)
    {
        if (player->IsPlayerBot())
            continue;
        Classes cls = Classes(player->getClass());
        switch (cls)
        {
            case CLASS_WARRIOR:
                return (player->FindTalentType() != 2);
            case CLASS_MAGE:
            case CLASS_WARLOCK:
            case CLASS_DRUID:
            case CLASS_DEATH_KNIGHT:
            case CLASS_PRIEST:
            case CLASS_ROGUE:
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
            case CLASS_PALADIN:
                break;
        }
    }
    return false;
}

bool Arena::ExistMightinessHealer(PLAYERS& players)
{
    for (Player* player : players)
    {
        if (!player->IsPlayerBot() || !IsHealerBot(player))
            continue;
        Classes cls = Classes(player->getClass());
        switch (cls)
        {
            case CLASS_PRIEST:
                return (player->FindTalentType() != 2);
            case CLASS_WARRIOR:
            case CLASS_MAGE:
            case CLASS_WARLOCK:
            case CLASS_DRUID:
            case CLASS_DEATH_KNIGHT:
            case CLASS_ROGUE:
            case CLASS_HUNTER:
            case CLASS_SHAMAN:
            case CLASS_PALADIN:
                break;
        }
        return false;
    }
    return false;
}

Player* Arena::FindPriorAttackTarget(PLAYERS& players)
{
    for (Player* target : players)
    {
        if (!CanSelectTarget(target))
            continue;
        Classes cls = Classes(target->getClass());
        if (cls == CLASS_WARLOCK)
            return target;
    }
    for (Player* target : players)
    {
        if (!CanSelectTarget(target))
            continue;
        Classes cls = Classes(target->getClass());
        if (cls == CLASS_WARRIOR)
            return target;
    }
    for (Player* target : players)
    {
        if (!CanSelectTarget(target))
            continue;
        Classes cls = Classes(target->getClass());
        if (cls == CLASS_MAGE)
            return target;
    }
    return NULL;
}

void Arena::NormalTactics(PLAYERS& comLists, PLAYERS& canSelectEnemys)
{
    for (Player* enemyPlayer : canSelectEnemys)
    {
        if (!IsHealerBot(enemyPlayer))
            continue;
        if (!CanSelectTarget(enemyPlayer))
            continue;
        for (Player* comPlayer : comLists)
            comPlayer->SetSelection(enemyPlayer->GetGUID());
        return;
    }
    if (Player* target = FindPriorAttackTarget(canSelectEnemys))
    {
        for (Player* comPlayer : comLists)
            comPlayer->SetSelection(target->GetGUID());
        return;
    }
    for (Player* enemyPlayer : canSelectEnemys)
    {
        if (!IsRangeBot(enemyPlayer))
            continue;
        if (!CanSelectTarget(enemyPlayer))
            continue;
        for (Player* comPlayer : comLists)
            comPlayer->SetSelection(enemyPlayer->GetGUID());
        return;
    }
    for (Player* enemyPlayer : canSelectEnemys)
    {
        if (!IsMeleeBot(enemyPlayer))
            continue;
        if (!CanSelectTarget(enemyPlayer))
            continue;
        for (Player* comPlayer : comLists)
            comPlayer->SetSelection(enemyPlayer->GetGUID());
        return;
    }
    for (Player* enemyPlayer : canSelectEnemys)
    {
        if (!CanSelectTarget(enemyPlayer))
            continue;
        for (Player* comPlayer : comLists)
            comPlayer->SetSelection(enemyPlayer->GetGUID());
        return;
    }
}

void Arena::InsureArenaAllPlayerFlag()
{
    for (auto itr = GetPlayers().begin(); itr != GetPlayers().end(); ++itr)
    {
        if (Player* player = ObjectAccessor::FindPlayer(itr->first))
            InsureArenaFlag(player);
    }
}

void Arena::InsureArenaFlag(Player* player, bool init)
{
    if (!player)
        return;

}

bool Arena::AssignTactics(PLAYERS& comLists, PLAYERS& enemyLists)
{
    if (BotUtility::BotArenaTeamTactics <= 0 || BotUtility::BotArenaTeamTactics > 2)
        return false;
    if (BotUtility::BotArenaTeamTactics == 1)
    {
        SuppressHealerPlayer(comLists, enemyLists);
        for (Player* enemyPlayer : enemyLists)
        {
            if (enemyPlayer->IsPlayerBot() || enemyPlayer->IsMounted())
                continue;
            if (!CanSelectTarget(enemyPlayer))
                continue;
            for (Player* comPlayer : comLists)
                comPlayer->SetSelection(enemyPlayer->GetGUID());
            return true;
        }
        NormalTactics(comLists, enemyLists);
    }
    else if (BotUtility::BotArenaTeamTactics == 2)
    {
        SuppressRealPlayer(comLists, enemyLists);
        NormalTactics(comLists, enemyLists);
    }
    return true;
}
