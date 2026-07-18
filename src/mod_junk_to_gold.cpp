#include "Chat.h"
#include "Player.h"
#include "ScriptMgr.h"

class JunkToGold : public PlayerScript
{
public:
    JunkToGold() : PlayerScript("JunkToGold") {}

    void OnPlayerLootItem(Player* player, Item* item, uint32 count, ObjectGuid /*lootguid*/) override
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        if (!proto)
        {
            return;
        }

        // Never sell quest items or items that start a quest, even if they are
        // gray quality (e.g. item 6196 "Noboru's Cudgel"). Guard against all
        // three ways an item can be quest-related: the quest item class, a
        // non-empty startquest, and quest-item bonding (4/5).
        if (proto->Class == ITEM_CLASS_QUEST ||
            proto->StartQuest != 0 ||
            proto->Bonding == BIND_QUEST_ITEM ||
            proto->Bonding == BIND_QUEST_ITEM1)
        {
            return;
        }

        if (proto->Quality == ITEM_QUALITY_POOR)
        {
            SendTransactionInformation(player, item, count);
            player->ModifyMoney(proto->SellPrice * count);
            player->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        }
    }

private:
    void SendTransactionInformation(Player* player, Item* item, uint32 count)
    {
        std::string name;
        if (count > 1)
        {
            name = Acore::StringFormat("|cff9d9d9d|Hitem:{}::::::::80:::::|h[{}]|h|rx{}", item->GetTemplate()->ItemId, item->GetTemplate()->Name1, count);
        }
        else
        {
            name = Acore::StringFormat("|cff9d9d9d|Hitem:{}::::::::80:::::|h[{}]|h|r", item->GetTemplate()->ItemId, item->GetTemplate()->Name1);
        }

        uint32 money = item->GetTemplate()->SellPrice * count;
        uint32 gold = money / GOLD;
        uint32 silver = (money % GOLD) / SILVER;
        uint32 copper = (money % GOLD) % SILVER;

        std::string info;
        if (money < SILVER)
        {
            info = Acore::StringFormat("{} sold for {} copper.", name, copper);
        }
        else if (money < GOLD)
        {
            if (copper > 0)
            {
                info = Acore::StringFormat("{} sold for {} silver and {} copper.", name, silver, copper);
            }
            else
            {
                info = Acore::StringFormat("{} sold for {} silver.", name, silver);
            }
        }
        else
        {
            if (copper > 0 && silver > 0)
            {
                info = Acore::StringFormat("{} sold for {} gold, {} silver and {} copper.", name, gold, silver, copper);
            }
            else if (copper > 0)
            {
                info = Acore::StringFormat("{} sold for {} gold and {} copper.", name, gold, copper);
            }
            else if (silver > 0)
            {
                info = Acore::StringFormat("{} sold for {} gold and {} silver.", name, gold, silver);
            }
            else
            {
                info = Acore::StringFormat("{} sold for {} gold.", name, gold);
            }
        }

        ChatHandler(player->GetSession()).SendSysMessage(info);
    }
};

void Addmod_junk_to_gold_Scripts()
{
    new JunkToGold();
}
