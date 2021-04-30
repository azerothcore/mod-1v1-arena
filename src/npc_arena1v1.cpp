/*
 *   Copyright (C) 2016+     AzerothCore <www.azerothcore.org>, released under GNU AGPL3 v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 *   Copyright (C) 2013      Emu-Devstore <http://emu-devstore.com/>
 *
 *   Written by Teiby <http://www.teiby.de/>
 *   Adjusted by fr4z3n for azerothcore
 *   Reworked by XDev
 */

#include "ScriptMgr.h"
#include "ArenaTeamMgr.h"
#include "DisableMgr.h"
#include "BattlegroundMgr.h"
#include "Battleground.h"
#include "Config.h"
#include "Language.h"
#include "Log.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "Chat.h"
#include "Tokenize.h"

namespace
{
    std::vector<uint32> _forbiddenTalents;

    bool HasForbiddenTalents(uint32 talentID)
    {
        for (auto const& itr : _forbiddenTalents)
            if (talentID == itr)
                return true;

        return false;
    }
};

constexpr BattlegroundQueueTypeId arenaQueue = (BattlegroundQueueTypeId)(BATTLEGROUND_QUEUE_5v5 + 1);

class Arena1v1_World : public WorldScript
{
public:
    Arena1v1_World() : WorldScript("Arena1v1_World") {}

    void OnAfterConfigLoad(bool /*Reload*/) override
    {
        // Clear before add for reload support
        _forbiddenTalents.clear();

        auto const frbiddenTalentsIDs = sConfigMgr->GetOption<std::string>("Arena1v1.ForbiddenTalentsIDs", "");

        for (auto const& talent : acore::Tokenize(frbiddenTalentsIDs, ',', true))
            _forbiddenTalents.emplace_back(talent);
    }
};

class Arena1v1_Player : public PlayerScript
{
public:
    Arena1v1_Player() : PlayerScript("Arena1v1_Player") { }

    void GetCustomGetArenaTeamId(const Player* player, uint8 slot, uint32& id) const override
    {
        if (slot == sConfigMgr->GetIntDefault("Arena1v1.ArenaSlotID", 3))
        {
            if (ArenaTeam* at = sArenaTeamMgr->GetArenaTeamByCaptain(player->GetGUID(), ARENA_TEAM_5v5))
            {
                id = at->GetId();
            }
        }
    }

    void GetCustomArenaPersonalRating(const Player* player, uint8 slot, uint32& rating) const override
    {
        if (slot == sConfigMgr->GetIntDefault("Arena1v1.ArenaSlotID", 3))
        {
            if (ArenaTeam* at = sArenaTeamMgr->GetArenaTeamByCaptain(player->GetGUID(), ARENA_TEAM_5v5))
            {
                rating = at->GetRating();
            }
        }
    }

    void OnGetMaxPersonalArenaRatingRequirement(const Player* player, uint32 minslot, uint32& maxArenaRating) const override
    {
        if (sConfigMgr->GetBoolDefault("Arena1v1.VendorRating", false) && minslot < (uint32)sConfigMgr->GetIntDefault("Arena1v1.ArenaSlotID", 3))
        {
            if (ArenaTeam* at = sArenaTeamMgr->GetArenaTeamByCaptain(player->GetGUID(), ARENA_TEAM_5v5))
            {
                maxArenaRating = std::max(at->GetRating(), maxArenaRating);
            }
        }
    }
};

class Arena1v1_Creature : public CreatureScript
{
public:
    Arena1v1_Creature() : CreatureScript("Arena1v1_Creature") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (!player || !creature)
            return true;

        if (!sConfigMgr->GetBoolDefault("Arena1v1.Enable", true))
        {
            ChatHandler(player->GetSession()).SendSysMessage("> Module arena 1v1 disabled!");
            return true;
        }

        if (player->InBattlegroundQueueForBattlegroundQueueType(arenaQueue))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Queue leave 1v1 Arena", GOSSIP_SENDER_MAIN, 3, "Are you sure?", 0, false);
        }
        else
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Queue enter 1v1 Arena (UnRated)", GOSSIP_SENDER_MAIN, 20);
        }

        if (!player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5)))
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Create new 1v1 Arena Team", GOSSIP_SENDER_MAIN, 1, "Are you sure?", sConfigMgr->GetIntDefault("Arena1v1.Costs", 400000), false);
        }
        else
        {
            if (!player->InBattlegroundQueueForBattlegroundQueueType(arenaQueue))
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Queue enter 1v1 Arena (Rated)", GOSSIP_SENDER_MAIN, 2);
                AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Arenateam Clear", GOSSIP_SENDER_MAIN, 5, "Are you sure?", 0, false);
            }

            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Shows your statistics", GOSSIP_SENDER_MAIN, 4);
        }

        SendGossipMenuFor(player, 68, creature);
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        if (!player || !creature)
            return true;

        ClearGossipMenuFor(player);

        ChatHandler handler(player->GetSession());

        switch (action)
        {
        case 1: // Create new Arenateam
        {
            if (sConfigMgr->GetIntDefault("Arena1v1.MinLevel", 80) <= player->getLevel())
            {
                if (player->GetMoney() >= uint32(sConfigMgr->GetIntDefault("Arena1v1.Costs", 400000)) && CreateArenateam(player, creature))
                    player->ModifyMoney(sConfigMgr->GetIntDefault("Arena1v1.Costs", 400000) * -1);
            }
            else
            {
                handler.PSendSysMessage("You have to be level %u + to create a 1v1 arena team.", sConfigMgr->GetIntDefault("Arena1v1.MinLevel", 70));
                CloseGossipMenuFor(player);
                return true;
            }
        }
        break;

        case 2: // Join Queue Arena (rated)
        {
            if (Arena1v1CheckTalents(player) && !JoinQueueArena(player, creature, true))
                handler.SendSysMessage("Something went wrong when joining the queue.");

            CloseGossipMenuFor(player);
            return true;
        }
        break;

        case 20: // Join Queue Arena (unrated)
        {
            if (Arena1v1CheckTalents(player) && !JoinQueueArena(player, creature, false))
                handler.SendSysMessage("Something went wrong when joining the queue.");

            CloseGossipMenuFor(player);
            return true;
        }
        break;

        case 3: // Leave Queue
        {
            uint8 arenaType = ARENA_TYPE_5v5;

            if (!player->InBattlegroundQueueForBattlegroundQueueType(arenaQueue))
                return true;

            WorldPacket data;
            data << arenaType << (uint8)0x0 << (uint32)BATTLEGROUND_AA << (uint16)0x0 << (uint8)0x0;
            player->GetSession()->HandleBattleFieldPortOpcode(data);
            CloseGossipMenuFor(player);
            return true;
        }
        break;

        case 4: // get statistics
        {
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5)));
            if (at)
            {
                std::stringstream s;
                s << "Rating: " << at->GetStats().Rating;
                s << "\nRank: " << at->GetStats().Rank;
                s << "\nSeason Games: " << at->GetStats().SeasonGames;
                s << "\nSeason Wins: " << at->GetStats().SeasonWins;
                s << "\nWeek Games: " << at->GetStats().WeekGames;
                s << "\nWeek Wins: " << at->GetStats().WeekWins;

                ChatHandler(player->GetSession()).PSendSysMessage(SERVER_MSG_STRING, s.str().c_str());
            }
        }
        break;

        case 5: // Disband arenateam
        {
            WorldPacket Data;
            Data << player->GetArenaTeamId(ArenaTeam::GetSlotByType(ARENA_TEAM_5v5));
            player->GetSession()->HandleArenaTeamLeaveOpcode(Data);
            handler.SendSysMessage("Arenateam deleted!");
            CloseGossipMenuFor(player);
            return true;
        }
        break;
        }

        OnGossipHello(player, creature);
        return true;
    }

private:
    bool JoinQueueArena(Player* player, Creature* me, bool isRated)
    {
        if (!player || !me)
            return false;

        if (sConfigMgr->GetIntDefault("Arena1v1.MinLevel", 80) > player->getLevel())
            return false;

        uint8 arenaslot = ArenaTeam::GetSlotByType(ARENA_TEAM_5v5);
        uint8 arenatype = ARENA_TYPE_5v5;
        uint32 arenaRating = 0;
        uint32 matchmakerRating = 0;

        // ignore if we already in BG or BG queue
        if (player->InBattleground())
            return false;

        //check existance
        Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
        if (!bg)
        {
            LOG_ERROR("modules", "Battleground: template bg (all arenas) not found");
            return false;
        }

        if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, BATTLEGROUND_AA, nullptr))
        {
            ChatHandler(player->GetSession()).PSendSysMessage(LANG_ARENA_DISABLED);
            return false;
        }

        PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), player->getLevel());
        if (!bracketEntry)
            return false;

        // check if already in queue
        if (player->GetBattlegroundQueueIndex(arenaQueue) < PLAYER_MAX_BATTLEGROUND_QUEUES)
            return false; // //player is already in this queue

        // check if has free queue slots
        if (!player->HasFreeBattlegroundQueueId())
            return false;

        uint32 ateamId = 0;

        if (isRated)
        {
            ateamId = player->GetArenaTeamId(arenaslot);
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(ateamId);
            if (!at)
            {
                player->GetSession()->SendNotInArenaTeamPacket(arenatype);
                return false;
            }

            // get the team rating for queueing
            arenaRating = at->GetRating();
            matchmakerRating = arenaRating;
            // the arenateam id must match for everyone in the group

            if (arenaRating <= 0)
                arenaRating = 1;
        }

        BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(arenaQueue);
        bgQueue.SetBgTypeIdAndArenaType(BATTLEGROUND_AA, ARENA_TYPE_5v5);
        bg->SetRated(isRated);
        bg->SetMaxPlayersPerTeam(1);

        GroupQueueInfo* ginfo = bgQueue.AddGroup(player, nullptr, bracketEntry, isRated, false, arenaRating, matchmakerRating, ateamId);
        uint32 avgTime = bgQueue.GetAverageQueueWaitTime(ginfo);
        uint32 queueSlot = player->AddBattlegroundQueueId(arenaQueue);

        // send status packet (in queue)
        WorldPacket data;
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_WAIT_QUEUE, avgTime, 0, arenatype, TEAM_NEUTRAL, isRated);
        player->GetSession()->SendPacket(&data);

        sBattlegroundMgr->ScheduleArenaQueueUpdate(matchmakerRating, arenaQueue, bracketEntry->GetBracketId());

        return true;
    }

    bool CreateArenateam(Player* player, Creature* me)
    {
        if (!player || !me)
            return false;

        uint8 slot = ArenaTeam::GetSlotByType(ARENA_TEAM_5v5);

        //Just to make sure as some other module might edit this value
        if (slot == 0)
            return false;

        // Check if player is already in an arena team
        if (player->GetArenaTeamId(slot))
        {
            player->GetSession()->SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, player->GetName(), "You are already in an arena team!", ERR_ALREADY_IN_ARENA_TEAM);
            return false;
        }

        // Teamname = playername
        // if teamname exist, we have to choose another name (playername + number)
        int i = 1;
        std::string teamName = player->GetName();
        do
        {
            if (sArenaTeamMgr->GetArenaTeamByName(teamName)) // teamname exist, so choose another name
                teamName = acore::StringFormat("%s - %u", player->GetName().c_str(), i++);
            else
                break;
        } while (i < 100); // should never happen

        // Create arena team
        ArenaTeam* arenaTeam = new ArenaTeam();
        if (!arenaTeam->Create(player->GetGUID(), ARENA_TEAM_5v5, teamName, 4283124816, 45, 4294242303, 5, 4294705149))
        {
            delete arenaTeam;
            return false;
        }

        // Register arena team
        sArenaTeamMgr->AddArenaTeam(arenaTeam);

        ChatHandler(player->GetSession()).SendSysMessage("1v1 Arenateam successfully created!");

        return true;
    }

    bool Arena1v1CheckTalents(Player* player)
    {
        if (!player)
            return false;

        if (!sConfigMgr->GetOption<bool>("Arena1v1.BlockForbiddenTalents", true))
            return true;

        uint32 count = 0;

        for (auto const& talentInfo : sTalentStore)
        {
            for (int8 rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
            {
                auto rankTalent = talentInfo->RankID[rank];

                if (!rankTalent)
                    continue;

                if (!player->HasTalent(rankTalent, player->GetActiveSpec()))
                    continue;

                if (HasForbiddenTalents(talentInfo->TalentTab))
                    count += rank + 1;
            }
        }

        if (count >= 36)
        {
            ChatHandler(player->GetSession()).SendSysMessage("You can not join because you have too many talent points in a forbidden tree. (Heal / Tank)");
            return false;
        }

        return true;
    }
};

class Arena1v1_ArenaTeam : public ArenaTeamScript
{
public:
    Arena1v1_ArenaTeam() : ArenaTeamScript("Arena1v1_ArenaTeam") {}

    void OnGetSlotByType(const uint32 type, uint8& slot) override
    {
        if (type == ARENA_TEAM_5v5)
        {
            slot = sConfigMgr->GetOption<uint8>("Arena1v1.ArenaSlotID", 3);
        }
    }

    void OnGetArenaPoints(ArenaTeam* at, float& points) override
    {
        if (at->GetType() == ARENA_TEAM_5v5)
        {
            points *= sConfigMgr->GetOption<float>("Arena1v1.ArenaPointsMulti", 0.64f);
        }
    }

    void OnTypeIDToQueueID(const BattlegroundTypeId, const uint8 arenaType, uint32& _bgQueueTypeId) override
    {
        if (arenaType == ARENA_TEAM_5v5)
        {
            _bgQueueTypeId = arenaQueue;
        }
    }

    void OnQueueIdToArenaType(const BattlegroundQueueTypeId _bgQueueTypeId, uint8& arenaType) override
    {
        if (_bgQueueTypeId == arenaQueue)
        {
            arenaType = ARENA_TEAM_5v5;
        }
    }

    void OnSetArenaMaxPlayersPerTeam(const uint8 type, uint32& maxPlayersPerTeam) override
    {
        if (type == ARENA_TEAM_5v5)
        {
            maxPlayersPerTeam = 1;
        }
    }
};

void AddSC_npc_1v1arena()
{
    new Arena1v1_World();
    new Arena1v1_Player();
    new Arena1v1_Creature();
    new Arena1v1_ArenaTeam();
}
