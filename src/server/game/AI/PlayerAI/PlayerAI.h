/*
 * Copyright (C) 2016-2016 TrinityCore <http://www.trinitycore.org/>
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

#ifndef TRINITY_PLAYERAI_H
#define TRINITY_PLAYERAI_H

#include "UnitAI.h"
#include "Player.h"
#include "Spell.h"
#include "Creature.h"

class SimpleCharmedPlayerAI : public PlayerAI
{
    public:
        SimpleCharmedPlayerAI(Player* player) : PlayerAI(player), _castCheckTimer(500), _chaseCloser(false), _forceFacing(true) { }
        void UpdateAI(uint32 diff) override;
        void OnCharmed(bool apply) override;

    protected:
        Unit* SelectAttackTarget() const override;

    private:
        TargetedSpell SelectAppropriateCastForSpec();
        uint32 _castCheckTimer;
        bool _chaseCloser;
        bool _forceFacing;
};

#endif
