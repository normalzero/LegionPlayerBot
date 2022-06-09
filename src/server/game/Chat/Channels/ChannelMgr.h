/*
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
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
#ifndef __TRINITY_CHANNELMGR_H
#define __TRINITY_CHANNELMGR_H

#include "Common.h"
#include "Channel.h"

#include "World.h"

#define MAX_CHANNEL_NAME_STR 31
#define MAX_CHANNEL_PASS_STR 31

class ChannelMgr
{
    public:
        ChannelMgr() { team = 0; }
        ~ChannelMgr();

        static ChannelMgr* forTeam(uint32 team)
        {
            static ChannelMgr allianceChannelMgr;
            static ChannelMgr hordeChannelMgr;
            if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHANNEL))
                return &allianceChannelMgr;        // cross-faction

            if (team == ALLIANCE)
                return &allianceChannelMgr;

            if (team == HORDE)
                return &hordeChannelMgr;

            return nullptr;
        }

        uint32 team;
        typedef std::map<std::wstring, Channel*> ChannelMap;

        Channel* GetJoinChannel(std::string name, uint32 channel_id);
        Channel* GetChannel(std::string const& name, Player* player, bool notify = true);
        void LeftChannel(std::string name);

    private:
        ChannelMap channels;
        static void SendNotOnChannelNotify(Player const* player, std::string const& name);
};

ChannelMgr* channelMgr(uint32 team);

#endif