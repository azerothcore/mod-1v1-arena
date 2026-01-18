#include "Chat.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Tokenize.h"
#include "DatabaseEnv.h"
#include "Config.h"
#include "BattlegroundMgr.h"
#include "CommandScript.h"
#include "npc_1v1arena.h"

using namespace Acore::ChatCommands;

class CommandJoin1v1 : public CommandScript
{
public:
    CommandJoin1v1() : CommandScript("CommandJoin1v1") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable command1v1Table =
        {
            { "rated",       HandleQueueArena1v1Rated,           SEC_PLAYER,        Console::No },
            { "unrated",     HandleQueueArena1v1UnRated,         SEC_PLAYER,        Console::No },
        };

        static ChatCommandTable commandTable =
        {
            { "q1v1", command1v1Table },
        };

        return commandTable;
    }

    static bool HandleQueueArena1v1Rated(ChatHandler* handler, const char* args)
    {
        return HandleQueueArena1v1(handler, args, true);
    }

    static bool HandleQueueArena1v1UnRated(ChatHandler* handler, const char* args)
    {
        return HandleQueueArena1v1(handler, args, false);
    }

    static bool HandleQueueArena1v1(ChatHandler* handler, const char* /*args*/, bool isRated)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        if (!sConfigMgr->GetOption<bool>("Arena1v1.EnableCommand", true))
        {
            handler->SendSysMessage("Join Arena 1v1 command is disabled.");
            return false;
        }

        if (!sConfigMgr->GetOption<bool>("Arena1v1.Enable", true))
        {
            handler->SendSysMessage("Arena 1v1 is disabled.");
            return false;
        }

        if (player->IsInCombat())
        {
            handler->SendSysMessage("Can't be in combat.");
            return false;
        }

        uint32 minLevel = sConfigMgr->GetOption<uint32>("Arena1v1.MinLevel", 80);
        if (player->GetLevel() < minLevel)
        {
            handler->PSendSysMessage("You need level {}+ to join solo arena.", minLevel);
            return false;
        }

        npc_1v1arena Command1v1;
        uint32 arenaSlot = sConfigMgr->GetOption<uint32>("Arena1v1.ArenaSlotID", 3);

        if (isRated)
        {
            if (!player->GetArenaTeamId(arenaSlot))
            {
                uint32 cost = sConfigMgr->GetOption<uint32>("Arena1v1.Costs", 400000);

                if (player->GetMoney() < cost)
                {
                    handler->PSendSysMessage("You need {} gold to create a 1v1 arena team.", cost / GOLD);
                    return false;
                }

                if (!Command1v1.CreateArenateam(player, nullptr))
                    return false;

                player->ModifyMoney(-int32(cost));

                handler->SendSysMessage("Arena 1v1 team created successfully. Use the command again to join the rated queue.");
                return true;
            }
        }

        if (Command1v1.JoinQueueArena(player, nullptr, isRated))
        {
            handler->PSendSysMessage("You have joined the solo 1v1 arena queue {}.", isRated ? "Rated" : "UnRated");
        }

        return true;
    }
};

void AddSC_arena1v1_commandscript()
{
    new CommandJoin1v1();
}
