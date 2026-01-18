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
#include "BattlegroundQueue.h"
#include "ArenaTeam.h"
#include "Language.h"
#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "ScriptedGossip.h"
#include "SharedDefines.h"
#include "Chat.h"
#include "npc_1v1arena.h"

#define NPC_TEXT_ENTRY_1v1 999992

//Const for 1v1 arena
constexpr uint32 ARENA_TEAM_1V1 = 1;
constexpr uint32 ARENA_TYPE_1V1 = 1;
constexpr uint32 BATTLEGROUND_QUEUE_1V1 = 11;
constexpr BattlegroundQueueTypeId bgQueueTypeId = (BattlegroundQueueTypeId)((int)BATTLEGROUND_QUEUE_5v5 + 1);
uint32 ARENA_SLOT_1V1 = 3;

//Config
std::vector<uint32> forbiddenTalents;

enum npcActions {
    NPC_ARENA_1V1_ACTION_CREATE_ARENA_TEAM = 1,
    NPC_ARENA_1V1_ACTION_JOIN_QUEUE_ARENA_RATED = 2,
    NPC_ARENA_1V1_ACTION_LEAVE_QUEUE = 3,
    NPC_ARENA_1V1_ACTION_GET_STATISTICS = 4,
    NPC_ARENA_1V1_ACTION_DISBAND_ARENA_TEAM = 5,
    NPC_ARENA_1V1_ACTION_JOIN_QUEUE_ARENA_UNRATED = 20,
    NPC_ARENA_1V1_MAIN_MENU = 21,
    NPC_ARENA_1V1_ACTION_HELP = 22,
};


bool teamExistForPlayerGuid(Player* player)
{
    QueryResult queryPlayerTeam = CharacterDatabase.Query("SELECT * FROM `arena_team` WHERE `captainGuid`={} AND `type`=1", player->GetGUID().GetCounter());
    if (queryPlayerTeam)
        return true;

    return false;
}

void deleteTeamArenaForPlayer(Player* player)
{
    QueryResult queryPlayerTeam = CharacterDatabase.Query("SELECT `arenaTeamId` FROM `arena_team` WHERE `captainGuid`={} AND `type`=1", player->GetGUID().GetCounter());
    if (queryPlayerTeam)
    {
        CharacterDatabase.Execute("DELETE FROM `arena_team` WHERE `captainGuid`={} AND `type`=1", player->GetGUID().GetCounter());
        sArenaTeamMgr->RemoveArenaTeam(player->GetArenaTeamId(ARENA_SLOT_1V1));
    }
}

class configloader_1v1arena : public WorldScript
{
public:
    configloader_1v1arena() : WorldScript("configloader_1v1arena", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD
    }) {}

    virtual void OnAfterConfigLoad(bool /*Reload*/) override
    {
        std::stringstream ss(sConfigMgr->GetOption<std::string>("Arena1v1.ForbiddenTalentsIDs", "0"));

        for (std::string blockedTalentsStr; std::getline(ss, blockedTalentsStr, ',');)
            forbiddenTalents.push_back(stoi(blockedTalentsStr));

        ARENA_SLOT_1V1 = sConfigMgr->GetOption<uint32>("Arena1v1.ArenaSlotID", 3);

        ArenaTeam::ArenaSlotByType.emplace(ARENA_TEAM_1V1, ARENA_SLOT_1V1);
        ArenaTeam::ArenaReqPlayersForType.emplace(ARENA_TYPE_1V1, 2);

        BattlegroundMgr::queueToBg.insert({ BATTLEGROUND_QUEUE_1V1,   BATTLEGROUND_AA });
        BattlegroundMgr::QueueToArenaType.emplace(BATTLEGROUND_QUEUE_1V1, (ArenaType) ARENA_TYPE_1V1);
        BattlegroundMgr::ArenaTypeToQueue.emplace(ARENA_TYPE_1V1, (BattlegroundQueueTypeId) BATTLEGROUND_QUEUE_1V1);
    }

};

class playerscript_1v1arena : public PlayerScript
{
public:
    playerscript_1v1arena() : PlayerScript("playerscript_1v1arena", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_GET_MAX_PERSONAL_ARENA_RATING_REQUIREMENT,
        PLAYERHOOK_ON_GET_ARENA_TEAM_ID
    }) { }

    void OnPlayerLogin(Player* pPlayer) override
    {
        if (sConfigMgr->GetOption<bool>("Arena1v1.Announcer", true))
            ChatHandler(pPlayer->GetSession()).SendSysMessage("This server is running the |cff4CFF00Arena 1v1 |rmodule.");
    }

    void OnPlayerGetMaxPersonalArenaRatingRequirement(const Player* player, uint32 minslot, uint32& maxArenaRating) const override
    {
        if (sConfigMgr->GetOption<bool>("Arena1v1.VendorRating", false) && minslot < (uint32)sConfigMgr->GetOption<uint32>("Arena1v1.ArenaSlotID", 3))
            if (ArenaTeam* at = sArenaTeamMgr->GetArenaTeamByCaptain(player->GetGUID(), ARENA_TEAM_1V1))
                maxArenaRating = std::max(at->GetRating(), maxArenaRating);
    }

    void OnPlayerGetArenaTeamId(Player* player, uint8 slot, uint32& result) override
    {
        if (!player)
            return;

        if (slot == ARENA_SLOT_1V1)
            result = player->GetArenaTeamIdFromDB(player->GetGUID(), ARENA_TYPE_1V1);
    }
};


bool npc_1v1arena::OnGossipHello(Player* player, Creature* creature)
{
    if (!player || !creature)
        return true;

    if (sConfigMgr->GetOption<bool>("Arena1v1.Enable", true) == false)
    {
        ChatHandler(player->GetSession()).SendSysMessage("1v1 disabled!");
        return true;
    }

    if (player->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId))
        AddGossipItemFor(player, GOSSIP_ICON_DOT, "|TInterface/ICONS/Achievement_Arena_2v2_7:30:30:-20:0|t Queue leave 1v1 Arena", GOSSIP_SENDER_MAIN, NPC_ARENA_1V1_ACTION_LEAVE_QUEUE, "Are you sure?", 0, false);
    else
        AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "|TInterface\\icons\\Achievement_Arena_2v2_4:30:30:-20:0|t Queue enter 1v1 Arena (UnRated)", GOSSIP_SENDER_MAIN, NPC_ARENA_1V1_ACTION_JOIN_QUEUE_ARENA_UNRATED);

    if (!teamExistForPlayerGuid(player))
    {
        AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "|TInterface/ICONS/Achievement_Arena_2v2_7:30:30:-20:0|t Create new 1v1 Arena Team", GOSSIP_SENDER_MAIN, NPC_ARENA_1V1_ACTION_CREATE_ARENA_TEAM, "Are you sure?", sConfigMgr->GetOption<uint32>("Arena1v1.Costs", 400000), false);
    }
    else
    {
        if (!player->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId))
        {
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "|TInterface\\icons\\Achievement_Arena_2v2_1:30:30:-20:0|t Queue enter 1v1 Arena (Rated)", GOSSIP_SENDER_MAIN, NPC_ARENA_1V1_ACTION_JOIN_QUEUE_ARENA_RATED);
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "|TInterface/ICONS/Achievement_Arena_2v2_7:30:30:-20:0|t Arenateam Clear", GOSSIP_SENDER_MAIN, NPC_ARENA_1V1_ACTION_DISBAND_ARENA_TEAM, "Are you sure?", 0, false);
        }

        AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "|TInterface/ICONS/INV_Misc_Coin_01:30:30:-20:0|t Shows your statistics", GOSSIP_SENDER_MAIN, NPC_ARENA_1V1_ACTION_GET_STATISTICS);
    }

    AddGossipItemFor(player, GOSSIP_ICON_VENDOR, "|TInterface/ICONS/inv_misc_questionmark:30:30:-20:0|t Help", GOSSIP_SENDER_MAIN, NPC_ARENA_1V1_ACTION_HELP);

    SendGossipMenuFor(player, 68, creature);
    return true;
}

bool npc_1v1arena::OnGossipSelect(Player* player, Creature* creature, uint32 /*sender*/, uint32 action)
{
    if (!player || !creature)
        return true;

    ClearGossipMenuFor(player);

    ChatHandler handler(player->GetSession());

    switch (action)
    {
        case NPC_ARENA_1V1_ACTION_CREATE_ARENA_TEAM:
        {
            if (sConfigMgr->GetOption<uint32>("Arena1v1.MinLevel", 80) <= player->GetLevel())
            {
                if (player->GetMoney() >= uint32(sConfigMgr->GetOption<uint32>("Arena1v1.Costs", 400000)) && CreateArenateam(player, creature))
                    player->ModifyMoney(sConfigMgr->GetOption<uint32>("Arena1v1.Costs", 400000) * -1);
            }
            else
            {
                handler.PSendSysMessage("You have to be level {} + to create a 1v1 arena team.", sConfigMgr->GetOption<uint32>("Arena1v1.MinLevel", 70));
                return true;
            }
            CloseGossipMenuFor(player);
        }
        break;

        case NPC_ARENA_1V1_ACTION_JOIN_QUEUE_ARENA_RATED:
        {
            if (Arena1v1CheckTalents(player) && !JoinQueueArena(player, creature, true))
                handler.SendSysMessage("Something went wrong when joining the queue.");

            CloseGossipMenuFor(player);
            return true;
        }


        case NPC_ARENA_1V1_ACTION_JOIN_QUEUE_ARENA_UNRATED:
        {
            if (Arena1v1CheckTalents(player) && !JoinQueueArena(player, creature, false))
                handler.SendSysMessage("Something went wrong when joining the queue.");

            CloseGossipMenuFor(player);
            return true;
        }

        case NPC_ARENA_1V1_ACTION_LEAVE_QUEUE:
        {
            uint8 arenaType = ARENA_TYPE_1V1;

            if (!player->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId))
                return true;

            WorldPacket data;
            data << arenaType << (uint8)0x0 << (uint32)BATTLEGROUND_AA << (uint16)0x0 << (uint8)0x0;
            player->GetSession()->HandleBattleFieldPortOpcode(data);
            CloseGossipMenuFor(player);
            return true;
        }


        case NPC_ARENA_1V1_ACTION_GET_STATISTICS:
        {
            ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(player->GetArenaTeamId(ARENA_SLOT_1V1));
            if (at)
            {
                std::stringstream s;
                s << "\nRating: " << at->GetStats().Rating;
                s << "\nRank: " << at->GetStats().Rank;
                s << "\nSeason Games: " << at->GetStats().SeasonGames;
                s << "\nSeason Wins: " << at->GetStats().SeasonWins;
                s << "\nWeek Games: " << at->GetStats().WeekGames;
                s << "\nWeek Wins: " << at->GetStats().WeekWins;

                ChatHandler(player->GetSession()).PSendSysMessage(SERVER_MSG_STRING, s.str().c_str());
            }
            CloseGossipMenuFor(player);
        }
        break;

        case NPC_ARENA_1V1_ACTION_DISBAND_ARENA_TEAM:
        {
            uint32 playerHonorPoints = player->GetHonorPoints();
            uint32 playerArenaPoints = player->GetArenaPoints();

            WorldPacket Data;
            Data << player->GetArenaTeamId(ARENA_SLOT_1V1);
            player->GetSession()->HandleArenaTeamLeaveOpcode(Data);
            handler.SendSysMessage("Arenateam deleted!");
            CloseGossipMenuFor(player);

            // hackfix: restore points
            player->SetHonorPoints(playerHonorPoints);
            player->SetArenaPoints(playerArenaPoints);

            return true;
        }

        case NPC_ARENA_1V1_ACTION_HELP:
        {
            AddGossipItemFor(player, GOSSIP_ICON_CHAT, "<- Back", GOSSIP_SENDER_MAIN, NPC_ARENA_1V1_MAIN_MENU);
            SendGossipMenuFor(player, NPC_TEXT_ENTRY_1v1, creature->GetGUID());
        }
        break;

        case NPC_ARENA_1V1_MAIN_MENU:
            OnGossipHello(player, creature);
            break;

    }

    return true;
}

bool npc_1v1arena::JoinQueueArena(Player* player, Creature* /* me */, bool isRated)
{
    if (!player)
        return false;

    if (sConfigMgr->GetOption<uint32>("Arena1v1.MinLevel", 80) > player->GetLevel())
        return false;

    uint8 arenatype = ARENA_TYPE_1V1;
    uint32 arenaRating = 0;
    uint32 matchmakerRating = 0;

    // ignore if we already in BG or BG queue
    if (player->InBattleground())
        return false;

    //check existance
    Battleground* bg = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
    if (!bg)
    {
        LOG_ERROR("module", "Battleground: template bg (all arenas) not found");
        return false;
    }

    if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, BATTLEGROUND_AA, nullptr))
    {
        ChatHandler(player->GetSession()).PSendSysMessage(LANG_ARENA_DISABLED);
        return false;
    }

    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bg->GetMapId(), player->GetLevel());
    if (!bracketEntry)
        return false;

    // check if already in queue
    if (player->GetBattlegroundQueueIndex(bgQueueTypeId) < PLAYER_MAX_BATTLEGROUND_QUEUES)
        return false; // //player is already in this queue

    // check if has free queue slots
    if (!player->HasFreeBattlegroundQueueId())
        return false;

    uint32 ateamId = 0;

    if (isRated)
    {
        ateamId = player->GetArenaTeamId(ARENA_SLOT_1V1);
        ArenaTeam* at = sArenaTeamMgr->GetArenaTeamById(ateamId);
        if (!at)
        {
            player->GetSession()->SendNotInArenaTeamPacket(arenatype);
            return false;
        }

        // get the team rating for queueing
        arenaRating = std::max(0u, at->GetRating());
        matchmakerRating = arenaRating;
        // the arenateam id must match for everyone in the group
    }

    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
    BattlegroundTypeId bgTypeId = BATTLEGROUND_AA;

    bg->SetRated(isRated);
    bg->SetMaxPlayersPerTeam(1);

    GroupQueueInfo* ginfo = bgQueue.AddGroup(player, nullptr, bgTypeId, bracketEntry, arenatype, isRated != 0, false, arenaRating, matchmakerRating, ateamId, 0);
    uint32 avgTime = bgQueue.GetAverageQueueWaitTime(ginfo);
    uint32 queueSlot = player->AddBattlegroundQueueId(bgQueueTypeId);

    // send status packet (in queue)
    WorldPacket data;
    sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, bg, queueSlot, STATUS_WAIT_QUEUE, avgTime, 0, arenatype, TEAM_NEUTRAL, isRated);
    player->GetSession()->SendPacket(&data);

    sBattlegroundMgr->ScheduleQueueUpdate(matchmakerRating, arenatype, bgQueueTypeId, bgTypeId, bracketEntry->GetBracketId());

    return true;
}

bool npc_1v1arena::CreateArenateam(Player* player, Creature* /* me */)
{
    if (!player)
        return false;

    uint8 slot = ArenaTeam::GetSlotByType(ARENA_TEAM_1V1);
    //Just to make sure as some other module might edit this value
    if (slot == 0)
        return false;

    // Check if player is already in an arena team
    if (player->GetArenaTeamId(slot))
    {
        player->GetSession()->SendArenaTeamCommandResult(ERR_ARENA_TEAM_CREATE_S, player->GetName(), "You are already in an arena team!", ERR_ALREADY_IN_ARENA_TEAM);
        return false;
    }

    // This disaster is the result of changing the MAX_ARENA_SLOT from 3 to 4.
    uint32 playerHonorPoints = player->GetHonorPoints();
    uint32 playerArenaPoints = player->GetArenaPoints();
    player->SetHonorPoints(0);
    player->SetArenaPoints(0);

    // This disaster is the result of changing the MAX_ARENA_SLOT from 3 to 4.
    deleteTeamArenaForPlayer(player);

    // Create arena team
    ArenaTeam* arenaTeam = new ArenaTeam();
    if (!arenaTeam->Create(player->GetGUID(), ARENA_TEAM_1V1, player->GetName(), 4283124816, 45, 4294242303, 5, 4294705149))
    {
        delete arenaTeam;

        // hackfix: restore points
        player->SetHonorPoints(playerHonorPoints);
        player->SetArenaPoints(playerArenaPoints);

        return false;
    }

    // Register arena team
    sArenaTeamMgr->AddArenaTeam(arenaTeam);

    ChatHandler(player->GetSession()).SendSysMessage("1v1 Arenateam successfully created!");

    // This disaster is the result of changing the MAX_ARENA_SLOT from 3 to 4.
    // hackfix: restore points
    player->SetHonorPoints(playerHonorPoints);
    player->SetArenaPoints(playerArenaPoints);

    return true;
}

bool npc_1v1arena::Arena1v1CheckTalents(Player* player)
{
    if (!player)
        return false;

    if (player->HasHealSpec() && (sConfigMgr->GetOption<bool>("Arena1v1.PreventHealingTalents", false)))
    {
        ChatHandler(player->GetSession()).SendSysMessage("You can't join because you have forbidden talents (Heal)");
        return false;
    }

    if (player->HasTankSpec() && (sConfigMgr->GetOption<bool>("Arena1v1.PreventTankTalents", false)))
    {
        ChatHandler(player->GetSession()).SendSysMessage("You can't join because you have forbidden talents (Tank)");
        return false;
    }

    return true;
}

class team_1v1arena : public ArenaTeamScript
{
public:
    team_1v1arena() : ArenaTeamScript("team_1v1arena", {
        ARENATEAMHOOK_ON_GET_SLOT_BY_TYPE,
        ARENATEAMHOOK_ON_GET_ARENA_POINTS,
        ARENATEAMHOOK_ON_TYPEID_TO_QUEUEID,
        ARENATEAMHOOK_ON_QUEUEID_TO_ARENA_TYPE,
        ARENATEAMHOOK_ON_SET_ARENA_MAX_PLAYERS_PER_TEAM
    }) {}

    void OnGetSlotByType(const uint32 type, uint8& slot) override
    {
        if (type == ARENA_TEAM_1V1)
        {
            slot = sConfigMgr->GetOption<uint32>("Arena1v1.ArenaSlotID", 3);
        }
    }

    void OnGetArenaPoints(ArenaTeam* at, float& points) override
    {
        if (at->GetType() == ARENA_TEAM_1V1)
        {
            const auto Members = at->GetMembers();
            uint8 playerLevel = sCharacterCache->GetCharacterLevelByGuid(Members.front().Guid);

            if (playerLevel >= sConfigMgr->GetOption<uint32>("Arena1v1.ArenaPointsMinLevel", 70))
                points *= sConfigMgr->GetOption<float>("Arena1v1.ArenaPointsMulti", 0.64f);
            else
                points *= 0;
        }
    }

    void OnTypeIDToQueueID(const BattlegroundTypeId, const uint8 arenaType, uint32& _bgQueueTypeId) override
    {
        if (arenaType == ARENA_TYPE_1V1)
        {
            _bgQueueTypeId = bgQueueTypeId;
        }
    }

    void OnQueueIdToArenaType(const BattlegroundQueueTypeId _bgQueueTypeId, uint8& arenaType) override
    {
        if (_bgQueueTypeId == bgQueueTypeId)
        {
            arenaType = ARENA_TYPE_1V1;
        }
    }

    void OnSetArenaMaxPlayersPerTeam(const uint8 type, uint32& maxPlayersPerTeam) override
    {
        if (type == ARENA_TYPE_1V1)
        {
            maxPlayersPerTeam = 1;
        }
    }
};

void AddSC_npc_1v1arena()
{
    new configloader_1v1arena();
    new playerscript_1v1arena();
    new npc_1v1arena();
    new team_1v1arena();
}
