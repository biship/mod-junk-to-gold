#include "Chat.h"
#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"

#ifdef MOD_PLAYERBOTS
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#endif

namespace
{
    static char const* const ITEM_LINK_MULTIPLE = "|cff9d9d9d|Hitem:{}::::::::{}:::::|h[{}]|h|rx{}";
    static char const* const ITEM_LINK_SINGLE = "|cff9d9d9d|Hitem:{}::::::::{}:::::|h[{}]|h|r";

    static char const* const SOLD_FOR_COPPER = "{}: {} sold for {} copper.";
    static char const* const SOLD_FOR_SILVER_COPPER = "{}: {} sold for {} silver and {} copper.";
    static char const* const SOLD_FOR_SILVER = "{}: {} sold for {} silver.";
    static char const* const SOLD_FOR_GOLD_SILVER_COPPER = "{}: {} sold for {} gold, {} silver and {} copper.";
    static char const* const SOLD_FOR_GOLD_COPPER = "{}: {} sold for {} gold and {} copper.";
    static char const* const SOLD_FOR_GOLD_SILVER = "{}: {} sold for {} gold and {} silver.";
    static char const* const SOLD_FOR_GOLD = "{}: {} sold for {} gold.";

    // For playerbots, route the sale chat message to the bot's owner session
    // instead of the bot's own (often null/headless) session.
    WorldSession* GetRecipientSession(Player* player)
    {
        if (!player)
        {
            return nullptr;
        }

#ifdef MOD_PLAYERBOTS
        PlayerbotAI* senderAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
        if (senderAI && senderAI->IsBotAI())
        {
            if (Player* master = senderAI->GetMaster())
            {
                if (WorldSession* masterSession = master->GetSession())
                {
                    return masterSession;
                }
            }
        }
#endif

        return player->GetSession();
    }
}

class JunkToGoldWorld : public WorldScript
{
public:
    JunkToGoldWorld() : WorldScript("JunkToGoldWorld") {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        // Print a log line on startup / reload (ModJunkToGold.Announce)
        if (sConfigMgr->GetOption<bool>("ModJunkToGold.Announce", true))
        {
            LOG_INFO("server.loading", "Using mod-junk-to-gold (junk items are automatically sold on loot).");
        }
    }
};

class JunkToGold : public PlayerScript
{
public:
    JunkToGold() : PlayerScript("JunkToGold") {}

    void OnPlayerLootItem(Player* player, Item* item, uint32 count, ObjectGuid /*lootguid*/) override
    {
        if (!sConfigMgr->GetOption<bool>("ModJunkToGold.Enable", true))
        {
            return;
        }

        bool enableForRealPlayer = sConfigMgr->GetOption<bool>("ModJunkToGold.EnableForRealPlayer", true);
        bool enableForPlayerbot = sConfigMgr->GetOption<bool>("ModJunkToGold.EnableForPlayerbot", true);
        if (!enableForRealPlayer || !enableForPlayerbot)
        {
            bool senderIsBot = false;
#ifdef MOD_PLAYERBOTS
            PlayerbotAI* senderAI = PlayerbotsMgr::instance().GetPlayerbotAI(player);
            senderIsBot = (senderAI && senderAI->IsBotAI());
#endif

            if (!enableForRealPlayer && !senderIsBot)
            {
                return;
            }

            if (!enableForPlayerbot && senderIsBot)
            {
                return;
            }
        }

        if (!item)
        {
            return;
        }

        ItemTemplate const* itemTemplate = item->GetTemplate();
        if (!itemTemplate)
        {
            return;
        }

        if (itemTemplate->Quality != ITEM_QUALITY_POOR)
        {
            return;
        }

        // Never sell quest items or items that start a quest, even if they are
        // gray quality (e.g. item 6196 "Noboru's Cudgel").
        if (IsQuestItem(itemTemplate))
        {
            return;
        }

        // Never sell an item that is required for one of the player's active quests.
        if (player->HasQuestForItem(itemTemplate->ItemId))
        {
            return;
        }

        if (IsSaleChatEnabled())
        {
            SendTransactionInformation(player, item, count);
        }

        player->ModifyMoney(int64(uint64(itemTemplate->SellPrice) * uint64(count)));

        // If the loot stacked into an existing slot, 'item' can represent a stack larger than 'count'.
        // DestroyItem(bag, slot) would delete the whole stack -> item loss.
        uint32 stackCount = item->GetCount();
        if (stackCount > count)
        {
            // Remove only the looted amount from the existing stack.
            item->SetCount(stackCount - count);
            item->SetState(ITEM_CHANGED, player);

            // Keep quest/item removal checks consistent with normal removals.
            player->ItemRemovedQuestCheck(item->GetEntry(), count);
        }
        else
        {
            // Full stack (or exact amount) -> remove the slot as before.
            player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        }
    }

private:
    static bool IsSaleChatEnabled()
    {
        // Print a log line on selling an item (ModJunkToGold.EnableSaleChat)
        return sConfigMgr->GetOption<bool>("ModJunkToGold.EnableSaleChat", true);
    }

    // Guard against all ways an item can be quest-related: the quest item class,
    // a non-empty startquest, and quest-item bonding (4/5).
    static bool IsQuestItem(ItemTemplate const* itemTemplate)
    {
        return itemTemplate->Class == ITEM_CLASS_QUEST ||
               itemTemplate->StartQuest != 0 ||
               itemTemplate->Bonding == BIND_QUEST_ITEM ||
               itemTemplate->Bonding == BIND_QUEST_ITEM1;
    }

    void SendTransactionInformation(Player* player, Item* item, uint32 count)
    {
        // Keep string templates centralized for easier localization migration.
        ItemTemplate const* itemTemplate = item->GetTemplate();
        uint8 linkLevel = player->GetLevel();
        std::string name;
        if (count > 1)
        {
            name = Acore::StringFormat(ITEM_LINK_MULTIPLE, itemTemplate->ItemId, linkLevel, itemTemplate->Name1, count);
        }
        else
        {
            name = Acore::StringFormat(ITEM_LINK_SINGLE, itemTemplate->ItemId, linkLevel, itemTemplate->Name1);
        }

        std::string seller = player->GetName();
        uint64 money = uint64(itemTemplate->SellPrice) * uint64(count);
        uint64 gold = money / GOLD;
        uint64 silver = (money % GOLD) / SILVER;
        uint64 copper = (money % GOLD) % SILVER;

        std::string info;
        if (money < SILVER)
        {
            info = Acore::StringFormat(SOLD_FOR_COPPER, seller, name, copper);
        }
        else if (money < GOLD)
        {
            if (copper > 0)
            {
                info = Acore::StringFormat(SOLD_FOR_SILVER_COPPER, seller, name, silver, copper);
            }
            else
            {
                info = Acore::StringFormat(SOLD_FOR_SILVER, seller, name, silver);
            }
        }
        else
        {
            if (copper > 0 && silver > 0)
            {
                info = Acore::StringFormat(SOLD_FOR_GOLD_SILVER_COPPER, seller, name, gold, silver, copper);
            }
            else if (copper > 0)
            {
                info = Acore::StringFormat(SOLD_FOR_GOLD_COPPER, seller, name, gold, copper);
            }
            else if (silver > 0)
            {
                info = Acore::StringFormat(SOLD_FOR_GOLD_SILVER, seller, name, gold, silver);
            }
            else
            {
                info = Acore::StringFormat(SOLD_FOR_GOLD, seller, name, gold);
            }
        }

        if (WorldSession* session = GetRecipientSession(player))
        {
            ChatHandler(session).SendSysMessage(info);
        }
    }
};

void Addmod_junk_to_goldScripts()
{
    new JunkToGoldWorld();
    new JunkToGold();
}
