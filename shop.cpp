#include "stdafx.h"
#include "../../libgame/include/grid.h"
#include "constants.h"
#include "utils.h"
#include "config.h"
#include "shop.h"
#include "desc.h"
#include "desc_manager.h"
#include "char.h"
#include "char_manager.h"
#include "item.h"
#include "item_manager.h"
#include "buffer_manager.h"
#include "packet.h"
#include "log.h"
#include "db.h"
#include "questmanager.h"
#include "monarch.h"
#include "mob_manager.h"
#include "locale_service.h"
#ifdef ENABLE_OFFLINE_SHOP
#include "desc_client.h"
#include "p2p.h"
#endif


/* ------------------------------------------------------------------------------------ */
CShop::CShop()
	: m_dwVnum(0), m_dwNPCVnum(0), m_pkPC(NULL)
#ifdef ENABLE_OFFLINE_SHOP
	, m_isOfflineShop(false)
#endif
{
#ifdef ENABLE_OFFLINE_SHOP
	m_pGrid = M2_NEW CGrid(10, 8);
#else
	m_pGrid = M2_NEW CGrid(5, 9);
#endif
}

#ifdef ENABLE_OFFLINE_SHOP
CShop::CShop(TOfflineShopTable *pTable)
	: m_dwVnum(0), m_dwNPCVnum(0), m_pkPC(NULL),
	m_table(*pTable), m_isOfflineShop(true), m_dwOwnerID(pTable->dwOwnerID),
	m_bIsLocked(pTable->bLocked), m_gold(pTable->gold)
{
	m_pGrid = M2_NEW CGrid(10, 8);
}
#endif

CShop::~CShop()
{
	TPacketGCShop pack;

	pack.header		= HEADER_GC_SHOP;
	pack.subheader	= SHOP_SUBHEADER_GC_END;
	pack.size		= sizeof(TPacketGCShop);

	Broadcast(&pack, sizeof(pack));

	GuestMapType::iterator it;

	it = m_map_guest.begin();

	while (it != m_map_guest.end())
	{
		LPCHARACTER ch = it->first;
		ch->SetShop(NULL);
		++it;
	}

	M2_DELETE(m_pGrid);
}

void CShop::SetPCShop(LPCHARACTER ch)
{
	m_pkPC = ch;
}

bool CShop::Create(DWORD dwVnum, DWORD dwNPCVnum, TShopItemTable * pTable)
{
	/*
	   if (NULL == CMobManager::instance().Get(dwNPCVnum))
	   {
	   sys_err("No such a npc by vnum %d", dwNPCVnum);
	   return false;
	   }
	 */
	sys_log(0, "SHOP #%d (Shopkeeper %d)", dwVnum, dwNPCVnum);

	m_dwVnum = dwVnum;
	m_dwNPCVnum = dwNPCVnum;

	BYTE bItemCount;

	for (bItemCount = 0; bItemCount < SHOP_HOST_ITEM_MAX_NUM; ++bItemCount)
		if (0 == (pTable + bItemCount)->vnum)
			break;

	SetShopItems(pTable, bItemCount);
	return true;
}

void CShop::SetShopItems(TShopItemTable * pTable, BYTE bItemCount)
{
	if (bItemCount > SHOP_HOST_ITEM_MAX_NUM)
		return;

	m_pGrid->Clear();

	m_itemVector.resize(SHOP_HOST_ITEM_MAX_NUM);
	memset(&m_itemVector[0], 0, sizeof(SHOP_ITEM) * m_itemVector.size());

	for (int i = 0; i < bItemCount; ++i)
	{
		LPITEM pkItem = NULL;
		const TItemTable * item_table;

		if (m_pkPC)
		{
			pkItem = m_pkPC->GetItem(pTable->pos);

			if (!pkItem)
			{
				sys_err("cannot find item on pos (%d, %d) (name: %s)", pTable->pos.window_type, pTable->pos.cell, m_pkPC->GetName());
				continue;
			}

			item_table = pkItem->GetProto();
		}
		else
		{
			if (!pTable->vnum)
				continue;

			item_table = ITEM_MANAGER::instance().GetTable(pTable->vnum);
		}

		if (!item_table)
		{
			sys_err("Shop: no item table by item vnum #%d", pTable->vnum);
			continue;
		}

		int iPos;

		if (IsPCShop())
		{
			sys_log(0, "MyShop: use position %d", pTable->display_pos);
			iPos = pTable->display_pos;
		}
		else
			iPos = m_pGrid->FindBlank(1, item_table->bSize);

		if (iPos < 0)
		{
			sys_err("not enough shop window");
			continue;
		}

		if (!m_pGrid->IsEmpty(iPos, 1, item_table->bSize))
		{
			if (IsPCShop())
			{
				sys_err("not empty position for pc shop %s[%d]", m_pkPC->GetName(), m_pkPC->GetPlayerID());
			}
			else
			{
				sys_err("not empty position for npc shop");
			}
			continue;
		}

		m_pGrid->Put(iPos, 1, item_table->bSize);

		SHOP_ITEM & item = m_itemVector[iPos];

		item.pkItem = pkItem;
		item.itemid = 0;

		if (item.pkItem)
		{
			item.vnum = pkItem->GetVnum();
			item.count = pkItem->GetCount(); // PC 샵의 경우 아이템 개수는 진짜 아이템의 개수여야 한다.
			item.price = pTable->price; // 가격도 사용자가 정한대로..
			item.itemid	= pkItem->GetID();
		}
		else
		{
			item.vnum = pTable->vnum;
			item.count = pTable->count;

			if (IS_SET(item_table->dwFlags, ITEM_FLAG_COUNT_PER_1GOLD))
			{
				if (item_table->dwGold == 0)
					item.price = item.count;
				else
					item.price = item.count / item_table->dwGold;
			}
			else
				item.price = item_table->dwGold * item.count;
		}

		char name[36];
		snprintf(name, sizeof(name), "%-20s(#%-5d) (x %d)", item_table->szName, (int) item.vnum, item.count);

		sys_log(0, "SHOP_ITEM: %-36s PRICE %-5d", name, item.price);
		++pTable;
	}
}

#ifdef ENABLE_YANG_LIMIT
long long CShop::Buy(LPCHARACTER ch, BYTE pos, bool bIsShopSearch)
#else
int CShop::Buy(LPCHARACTER ch, BYTE pos, bool bIsShopSearch)
#endif
{
	if (pos >= m_itemVector.size())
	{
		sys_log(0, "Shop::Buy : invalid position %d : %s", pos, ch->GetName());
		return SHOP_SUBHEADER_GC_INVALID_POS;
	}

	sys_log(0, "Shop::Buy : name %s pos %d", ch->GetName(), pos);

	GuestMapType::iterator it = m_map_guest.find(ch);

#if defined(BL_PRIVATESHOP_SEARCH_SYSTEM)
	if (bIsShopSearch == false && it == m_map_guest.end())
		return SHOP_SUBHEADER_GC_END;
#else
	if (it == m_map_guest.end())
		return SHOP_SUBHEADER_GC_END;
#endif

	SHOP_ITEM& r_item = m_itemVector[pos];

	if (r_item.price <= 0)
	{
		LogManager::instance().HackLog("SHOP_BUY_GOLD_OVERFLOW", ch);
		return SHOP_SUBHEADER_GC_NOT_ENOUGH_MONEY;
	}

	LPITEM pkSelectedItem = ITEM_MANAGER::instance().Find(r_item.itemid);

	if (IsPCShop())
	{
		if (!pkSelectedItem)
		{
#if defined(BL_PRIVATESHOP_SEARCH_SYSTEM)
			if (bIsShopSearch == true)
				return SHOP_SUBHEADER_GC_SOLDOUT;
#endif
			sys_log(0, "Shop::Buy : Critical: This user seems to be a hacker : invalid pcshop item : BuyerPID:%d SellerPID:%d",
					ch->GetPlayerID(),
					m_pkPC->GetPlayerID());
			return false;
		}

		if ((pkSelectedItem->GetOwner() != m_pkPC))
		{
#if defined(BL_PRIVATESHOP_SEARCH_SYSTEM)
			if (bIsShopSearch == true)
				return SHOP_SUBHEADER_GC_SOLDOUT;
#endif
			sys_log(0, "Shop::Buy : Critical: This user seems to be a hacker : invalid pcshop item : BuyerPID:%d SellerPID:%d",
					ch->GetPlayerID(),
					m_pkPC->GetPlayerID());

			return false;
		}
	}

#ifdef ENABLE_YANG_LIMIT
	long long dwPrice = r_item.price;
#else
	DWORD dwPrice = r_item.price;
#endif

#if defined(BL_PRIVATESHOP_SEARCH_SYSTEM)
	if (bIsShopSearch == false && it->second)	// if other empire, price is triple
		dwPrice *= 3;
#else
	if (it->second)	// if other empire, price is triple
		dwPrice *= 3;
#endif

#ifdef ENABLE_YANG_LIMIT
	if (ch->GetGold() < (long long) dwPrice)
	{
		sys_log(1, "Shop::Buy : Not enough money : %s has %lld, price %lld", ch->GetName(), ch->GetGold(), dwPrice);
		return SHOP_SUBHEADER_GC_NOT_ENOUGH_MONEY;
	}
#else
	if (ch->GetGold() < (int) dwPrice)
	{
		sys_log(1, "Shop::Buy : Not enough money : %s has %d, price %d", ch->GetName(), ch->GetGold(), dwPrice);
		return SHOP_SUBHEADER_GC_NOT_ENOUGH_MONEY;
	}
#endif

	LPITEM item;

	if (m_pkPC) // 피씨가 운영하는 샵은 피씨가 실제 아이템을 가지고있어야 한다.
		item = r_item.pkItem;
	else
		item = ITEM_MANAGER::instance().CreateItem(r_item.vnum, r_item.count);

	if (!item)
		return SHOP_SUBHEADER_GC_SOLD_OUT;

	/*if (!m_pkPC)
	{
		if (quest::CQuestManager::instance().GetEventFlag("hivalue_item_sell") == 0)
		{
			if (item->GetVnum() == 70024 || item->GetVnum() == 70035)
			{
				return SHOP_SUBHEADER_GC_END;
			}
		}
	}*/

	int iEmptyPos;
	if (item->IsDragonSoul())
	{
		iEmptyPos = ch->GetEmptyDragonSoulInventory(item);
	}
	else
	{
		iEmptyPos = ch->GetEmptyInventory(item->GetSize());
	}

	if (iEmptyPos < 0)
	{
		if (m_pkPC)
		{
			sys_log(1, "Shop::Buy at PC Shop : Inventory full : %s size %d", ch->GetName(), item->GetSize());
			return SHOP_SUBHEADER_GC_INVENTORY_FULL;
		}
		else
		{
			sys_log(1, "Shop::Buy : Inventory full : %s size %d", ch->GetName(), item->GetSize());
			M2_DESTROY_ITEM(item);
			return SHOP_SUBHEADER_GC_INVENTORY_FULL;
		}
	}

	ch->PointChange(POINT_GOLD, -dwPrice, false);

	//세금 계산
	DWORD dwTax = 0;
	int iVal = 0;

	{
		iVal = quest::CQuestManager::instance().GetEventFlag("personal_shop");

		if (0 < iVal)
		{
			if (iVal > 100)
				iVal = 100;

			dwTax = dwPrice * iVal / 100;
			dwPrice = dwPrice - dwTax;
		}
		else
		{
			iVal = 0;
			dwTax = 0;
		}
	}

	if (!m_pkPC) 
	{
		CMonarch::instance().SendtoDBAddMoney(dwTax, ch->GetEmpire(), ch);
	}

	if (m_pkPC)
	{
		m_pkPC->SyncQuickslot(QUICKSLOT_TYPE_ITEM, item->GetCell(), 255);

		if (item->GetVnum() == 90008 || item->GetVnum() == 90009) // VCARD
		{
			VCardUse(m_pkPC, ch, item);
			item = NULL;
		}
		else
		{
			char buf[512];

			if (item->GetVnum() >= 80003 && item->GetVnum() <= 80007)
			{
#ifdef ENABLE_YANG_LIMIT
				snprintf(buf, sizeof(buf), "%s FROM: %u TO: %u PRICE: %lld", item->GetName(), ch->GetPlayerID(), m_pkPC->GetPlayerID(), dwPrice);
#else
				snprintf(buf, sizeof(buf), "%s FROM: %u TO: %u PRICE: %u", item->GetName(), ch->GetPlayerID(), m_pkPC->GetPlayerID(), dwPrice);
#endif
				LogManager::instance().GoldBarLog(ch->GetPlayerID(), item->GetID(), SHOP_BUY, buf);
				LogManager::instance().GoldBarLog(m_pkPC->GetPlayerID(), item->GetID(), SHOP_SELL, buf);
			}
			
			item->RemoveFromCharacter();
			if (item->IsDragonSoul())
				item->AddToCharacter(ch, TItemPos(DRAGON_SOUL_INVENTORY, iEmptyPos));
			else
				item->AddToCharacter(ch, TItemPos(INVENTORY, iEmptyPos));
			ITEM_MANAGER::instance().FlushDelayedSave(item);
			
#ifdef ENABLE_YANG_LIMIT
			snprintf(buf, sizeof(buf), "%s %u(%s) %lld %u", item->GetName(), m_pkPC->GetPlayerID(), m_pkPC->GetName(), dwPrice, item->GetCount());
			LogManager::instance().ItemLog(ch, item, "SHOP_BUY", buf);

			snprintf(buf, sizeof(buf), "%s %u(%s) %lld %u", item->GetName(), ch->GetPlayerID(), ch->GetName(), dwPrice, item->GetCount());
			LogManager::instance().ItemLog(m_pkPC, item, "SHOP_SELL", buf);
#else
			snprintf(buf, sizeof(buf), "%s %u(%s) %u %u", item->GetName(), m_pkPC->GetPlayerID(), m_pkPC->GetName(), dwPrice, item->GetCount());
			LogManager::instance().ItemLog(ch, item, "SHOP_BUY", buf);

			snprintf(buf, sizeof(buf), "%s %u(%s) %u %u", item->GetName(), ch->GetPlayerID(), ch->GetName(), dwPrice, item->GetCount());
			LogManager::instance().ItemLog(m_pkPC, item, "SHOP_SELL", buf);
#endif
		}

#if defined(ENABLE_SEARCH_SHOP) || defined(ENABLE_OFFLINE_SHOP)
		if (IsPCShop())
			memset(&r_item, 0, sizeof(r_item));
#else
		r_item.pkItem = NULL;
#endif

		r_item.pkItem = NULL;
		BroadcastUpdateItem(pos);

#ifdef ENABLE_OFFLINE_SHOP
		if (m_isOfflineShop)
		{
			m_pGrid->Get(pos, 1, item->GetSize());
			m_table.items[pos].vnum = 0;
			m_table.items[pos].price = 0;
			m_gold += dwPrice;

			BroadcastPacket(UPDATE_GOLD, &m_gold);

			Save(true);
		}
		else
		{
			m_pkPC->PointChange(POINT_GOLD, dwPrice, false);

			if (iVal > 0)
				m_pkPC->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[LC]747"), iVal);

			CMonarch::instance().SendtoDBAddMoney(dwTax, m_pkPC->GetEmpire(), m_pkPC);
		}
#else
		m_pkPC->PointChange(POINT_GOLD, dwPrice, false);

		if (iVal > 0)
			m_pkPC->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[LC]747"), iVal);

		CMonarch::instance().SendtoDBAddMoney(dwTax, m_pkPC->GetEmpire(), m_pkPC);
#endif

	}
	else
	{
		if (item->IsDragonSoul())
			item->AddToCharacter(ch, TItemPos(DRAGON_SOUL_INVENTORY, iEmptyPos));
		else
			item->AddToCharacter(ch, TItemPos(INVENTORY, iEmptyPos));
		ITEM_MANAGER::instance().FlushDelayedSave(item);
		LogManager::instance().ItemLog(ch, item, "BUY", item->GetName());

		if (item->GetVnum() >= 80003 && item->GetVnum() <= 80007)
		{
			LogManager::instance().GoldBarLog(ch->GetPlayerID(), item->GetID(), PERSONAL_SHOP_BUY, "");
		}

		DBManager::instance().SendMoneyLog(MONEY_LOG_SHOP, item->GetVnum(), -dwPrice);
	}

#ifdef ENABLE_YANG_LIMIT
	if (item)
		sys_log(0, "SHOP: BUY: name %s %s(x %d):%u price %lld", ch->GetName(), item->GetName(), item->GetCount(), item->GetID(), dwPrice);
#else
	if (item)
		sys_log(0, "SHOP: BUY: name %s %s(x %d):%u price %u", ch->GetName(), item->GetName(), item->GetCount(), item->GetID(), dwPrice);
#endif

	ch->Save();

	return (SHOP_SUBHEADER_GC_OK);
}

bool CShop::AddGuest(LPCHARACTER ch, DWORD owner_vid, bool bOtherEmpire)
{
	if (!ch)
		return false;

	if (ch->GetExchange())
		return false;

	if (ch->GetShop())
		return false;

#ifdef ENABLE_OFFLINE_SHOP
	if (m_isOfflineShop && m_bIsLocked && m_dwOwnerID != ch->GetPlayerID())
		return false;

	if (ch->GetPlayerID() == m_dwOwnerID)
		ch->SetOfflineShopTable(&m_table);
#endif

	ch->SetShop(this);

	m_map_guest.insert(GuestMapType::value_type(ch, bOtherEmpire));

	TPacketGCShop pack;

	pack.header		= HEADER_GC_SHOP;
	pack.subheader	= SHOP_SUBHEADER_GC_START;

	TPacketGCShopStart pack2;

	memset(&pack2, 0, sizeof(pack2));
	pack2.owner_vid = owner_vid;

#ifdef ENABLE_OFFLINE_SHOP
	pack2.bIsOwner = (m_isOfflineShop && m_dwOwnerID == ch->GetPlayerID());
	pack2.gold = (m_dwOwnerID ? m_gold : 0);
	pack2.bLocked = (m_dwOwnerID ? m_bIsLocked : false);
#endif


	for (DWORD i = 0; i < m_itemVector.size() && i < SHOP_HOST_ITEM_MAX_NUM; ++i)
	{
		const SHOP_ITEM & item = m_itemVector[i];

		if (item.vnum == 70024 || item.vnum == 70035)
		{
			continue;
		}

		if (m_pkPC && !item.pkItem)
			continue;

		pack2.items[i].vnum = item.vnum;

#ifdef ENABLE_NEWSTUFF
		if (bOtherEmpire && !g_bEmpireShopPriceTripleDisable)
#else
		if (bOtherEmpire)
#endif
		{
			pack2.items[i].price = item.price * 3;
		}
		else
			pack2.items[i].price = item.price;

		pack2.items[i].count = item.count;

		if (item.pkItem)
		{
			thecore_memcpy(pack2.items[i].alSockets, item.pkItem->GetSockets(), sizeof(pack2.items[i].alSockets));
			thecore_memcpy(pack2.items[i].aAttr, item.pkItem->GetAttributes(), sizeof(pack2.items[i].aAttr));
		}
	}

	pack.size = sizeof(pack) + sizeof(pack2);

	ch->GetDesc()->BufferedPacket(&pack, sizeof(TPacketGCShop));
	ch->GetDesc()->Packet(&pack2, sizeof(TPacketGCShopStart));
	return true;
}

void CShop::RemoveGuest(LPCHARACTER ch)
{
	if (ch->GetShop() != this)
		return;

	m_map_guest.erase(ch);
	ch->SetShop(NULL);

	TPacketGCShop pack;

	pack.header		= HEADER_GC_SHOP;
	pack.subheader	= SHOP_SUBHEADER_GC_END;
	pack.size		= sizeof(TPacketGCShop);

	ch->GetDesc()->Packet(&pack, sizeof(pack));
}

void CShop::Broadcast(const void * data, int bytes)
{
	sys_log(1, "Shop::Broadcast %p %d", data, bytes);

	GuestMapType::iterator it;

	it = m_map_guest.begin();

	while (it != m_map_guest.end())
	{
		LPCHARACTER ch = it->first;

		if (ch->GetDesc())
			ch->GetDesc()->Packet(data, bytes);

		++it;
	}
}

void CShop::BroadcastUpdateItem(BYTE pos)
{
	TPacketGCShop pack;
	TPacketGCShopUpdateItem pack2;

	TEMP_BUFFER	buf;

	pack.header		= HEADER_GC_SHOP;
	pack.subheader	= SHOP_SUBHEADER_GC_UPDATE_ITEM;
	pack.size		= sizeof(pack) + sizeof(pack2);

	pack2.pos		= pos;

	if (m_pkPC && !m_itemVector[pos].pkItem)
		pack2.item.vnum = 0;
	else
	{
		pack2.item.vnum	= m_itemVector[pos].vnum;
		if (m_itemVector[pos].pkItem)
		{
			thecore_memcpy(pack2.item.alSockets, m_itemVector[pos].pkItem->GetSockets(), sizeof(pack2.item.alSockets));
			thecore_memcpy(pack2.item.aAttr, m_itemVector[pos].pkItem->GetAttributes(), sizeof(pack2.item.aAttr));
		}
		else
		{
			memset(pack2.item.alSockets, 0, sizeof(pack2.item.alSockets));
			memset(pack2.item.aAttr, 0, sizeof(pack2.item.aAttr));
		}
	}

	pack2.item.price	= m_itemVector[pos].price;
	pack2.item.count	= m_itemVector[pos].count;

	buf.write(&pack, sizeof(pack));
	buf.write(&pack2, sizeof(pack2));

	Broadcast(buf.read_peek(), buf.size());
}

int CShop::GetNumberByVnum(DWORD dwVnum)
{
	int itemNumber = 0;

	for (DWORD i = 0; i < m_itemVector.size() && i < SHOP_HOST_ITEM_MAX_NUM; ++i)
	{
		const SHOP_ITEM & item = m_itemVector[i];

		if (item.vnum == dwVnum)
		{
			itemNumber += item.count;
		}
	}

	return itemNumber;
}

bool CShop::IsSellingItem(DWORD itemID)
{
	bool isSelling = false;

	for (DWORD i = 0; i < m_itemVector.size() && i < SHOP_HOST_ITEM_MAX_NUM; ++i)
	{
		if (static_cast<unsigned long>(m_itemVector[i].itemid) == itemID)
		{
			isSelling = true;
			break;
		}
	}

	return isSelling;

}

#ifdef ENABLE_OFFLINE_SHOP
void CShop::OnClose(LPCHARACTER ch)
{
	if (!m_isOfflineShop || ch->GetPlayerID() != m_dwOwnerID)
		return;

	GuestMapType::iterator it = m_map_guest.find(ch);

	if (it == m_map_guest.end())
		return;

	const int64_t nTotalMoney = static_cast<int64_t>(ch->GetGold()) + static_cast<int64_t>(m_gold);

	if (GOLD_MAX <= nTotalMoney)
	{
		ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[LC]748"));
		return;
	}

	for (BYTE i = 0; i < m_itemVector.size() && i < SHOP_HOST_ITEM_MAX_NUM; ++i)
	{
		LPITEM pkItem = m_itemVector[i].pkItem;

		if (!pkItem)
			continue;

		int iEmptyPos = 0;

		if (pkItem->IsDragonSoul())
			iEmptyPos = ch->GetEmptyDragonSoulInventory(pkItem);
		else
			iEmptyPos = ch->GetEmptyInventory(pkItem->GetSize());

		if (iEmptyPos == -1)
		{
			ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[LC]749"));
			return;
		}
	}

	ch->PointChange(POINT_GOLD, m_gold, false);
	m_gold = 0;

	for (BYTE i = 0; i < m_itemVector.size() && i < SHOP_HOST_ITEM_MAX_NUM; ++i)
	{
		LPITEM pkItem = m_itemVector[i].pkItem;

		if (!pkItem)
			continue;

		pkItem->RemoveFromCharacter();
		ch->AutoGiveItem(pkItem);
		m_itemVector[i].pkItem = NULL;
		m_table.items[i].vnum = 0;
	}

	Save(true);
}

bool CShop::IsEmpty()
{
	for (BYTE i = 0; i < m_itemVector.size() && i < SHOP_HOST_ITEM_MAX_NUM; ++i)
	{
		if (m_itemVector[i].pkItem)
			return false;
	}

	return true;
}

void CShop::SetLock(bool bLock)
{
	if (m_bIsLocked && (DWORD)get_global_time() >= m_table.dwTimeLeft)
	{
		m_table.dwTimeLeft = get_global_time() + OFFLINE_SHOP_TIME_LIMIT;
		m_pkPC->OfflineShopRenew();
		DWORD time = m_table.dwTimeLeft - get_global_time();
		BroadcastPacket(UPDATE_TIME, &time);
	}

	if (IsEmpty() && m_bIsLocked)
		return;

	m_bIsLocked = bLock; Save();

	if (m_bIsLocked)
	{
		for (GuestMapType::iterator it = m_map_guest.begin(); it != m_map_guest.end();)
		{
			LPCHARACTER ch2 = it->first;

			if (ch2->GetPlayerID() != m_dwOwnerID)
			{
				if (ch2->GetShop() != this)
					return;

				it = m_map_guest.erase(it);
				ch2->SetShop(NULL);

				TPacketGCShop pack;

				pack.header = HEADER_GC_SHOP;
				pack.subheader = SHOP_SUBHEADER_GC_END;
				pack.size = sizeof(TPacketGCShop);

				ch2->GetDesc()->Packet(&pack, sizeof(pack));
			}
			else
				++it;
		}
	}

	m_pkPC->SetOnlyView(bLock ? m_dwOwnerID : 0);

	BroadcastPacket(SET_LOCK, &bLock);
}

void CShop::ChangeSign(const char *sign)
{
	m_pkPC->SetShopSign(sign);
	strlcpy(m_table.szSign, sign, strlen(sign) + 1);

	BroadcastPacket(UPDATE_SIGN, sign);
	Save();
}

void CShop::WithdrawGold(uGoldType gold)
{
	if (m_gold < gold)
		return;

	BroadcastPacket(SEND_GOLD, &gold);
	BroadcastPacket(UPDATE_GOLD, &m_gold);

	Save(true);
}

void CShop::WithdrawItem(BYTE pos)
{
	if (pos >= m_itemVector.size())
		return;

	LPITEM pItem = m_itemVector[pos].pkItem;

	if (!pItem)
		return;

	if (pItem->GetOwner() != m_pkPC)
		return;

	m_pGrid->Get(pos, 1, pItem->GetSize());


	memset(&m_itemVector[pos], 0, sizeof(m_itemVector[pos]));
	m_table.items[pos].vnum = 0;
	m_table.items[pos].price = 0;

	pItem->RemoveFromCharacter();

	BroadcastPacket(SEND_ITEM, pItem);
	BroadcastUpdateItem(pos);

	Save(true);
}

void CShop::AddItem(TPlayerItem *item)
{
	LPITEM pkItem = ITEM_MANAGER::instance().CreateItem(item->vnum, item->count, item->id);
	
	if (!pkItem)
	{
		sys_err("Can't create item! ID = %u", item->id);
		return;
	}

	pkItem->SetSockets(item->alSockets);
	pkItem->SetAttributes(item->aAttr);

	int iPos = item->pos;

	if (!m_pGrid->IsEmpty(iPos, 1, pkItem->GetSize()))
	{
		BroadcastPacket(SEND_ITEM, pkItem);
		sys_err("no space! ID = %u", item->id);
		return;
	}

	int64_t nTotalMoney = static_cast<int64_t>(m_gold) + static_cast<int64_t>(item->price);

	for (BYTE i = 0; i < SHOP_HOST_ITEM_MAX_NUM; ++i)
		nTotalMoney += static_cast<int64_t>(m_itemVector[i].price);

	if (GOLD_MAX <= nTotalMoney)
	{
		BroadcastPacket(SEND_ITEM, pkItem);
		sys_err("over 2kkk! ID = %u, total %u", item->id, nTotalMoney);
		return;
	}

	pkItem->SendToOfflineShop(m_pkPC, item->owner, item->price, item->pos);

	m_pGrid->Put(iPos, 1, pkItem->GetSize());
	m_itemVector[iPos] = SHOP_ITEM(pkItem->GetVnum(), item->price, pkItem->GetCount(), pkItem, pkItem->GetID());
	m_table.items[iPos] = *item;

	BroadcastUpdateItem(iPos); Save();
}

void CShop::BroadcastPacket(BYTE type, const void *data)
{
	LPCHARACTER ch = CHARACTER_MANAGER::instance().FindByPID(m_dwOwnerID);

	switch (type)
	{
		case SET_LOCK:
		case UPDATE_TIME:
		case UPDATE_SIGN:
		case UPDATE_GOLD:
		{
			if (!ch)
				return;

			GuestMapType::iterator it = m_map_guest.find(ch);

			if (it == m_map_guest.end())
				return;

			TPacketGCShop pack;
			TEMP_BUFFER buf;

			pack.header = HEADER_GC_SHOP;

			BYTE subHeader = 0, size = 0;

			if (type == SET_LOCK)
			{ 
				subHeader = SHOP_SUBHEADER_GC_SET_LOCK;
				size = sizeof(bool);
			}
			else if (type == UPDATE_SIGN)
			{
				subHeader = SHOP_SUBHEADER_GC_UPDATE_SIGN;
				size = strlen((char*)data) + 1;

			}
			else if (type == UPDATE_GOLD)
			{
				subHeader = SHOP_SUBHEADER_GC_UPDATE_GOLD;
				size = sizeof(uGoldType);
			}
			else
			{
				subHeader = SHOP_SUBHEADER_GC_UPDATE_TIME;
				size = sizeof(DWORD);
			}

			pack.subheader = subHeader;
			pack.size = sizeof(pack) + size;

			buf.write(&pack, sizeof(pack));
			buf.write(data, size);

			ch->GetDesc()->Packet(buf.read_peek(), buf.size());

			return;
		}
	}

	CCI *pcci = NULL; TPacketGGOfflineShop pack; TEMP_BUFFER buf;

	if (!ch)
	{
		pcci = P2P_MANAGER::instance().FindByPID(m_dwOwnerID);

		if (!pcci)
			return;
		
		pack.header = HEADER_GG_OFFLINE_SHOP;
		pack.ownerID = m_dwOwnerID;
	}

	switch (type)
	{
		case UPDATE_TABLE:
		{
			if (ch)
			{
				GuestMapType::iterator it = m_map_guest.find(ch);

				BYTE mode = 1;

				if (it != m_map_guest.end())
					mode = 2;

				ch->SetOfflineShopTable(&m_table, mode);

				return;
			}

			pack.subHeader = SUBHEADER_GG_OFFLINE_SHOP_UPDATE;

			buf.write(&pack, sizeof(TPacketGGOfflineShop));
			buf.write(&m_table, sizeof(TOfflineShopTable));

			pcci->pkDesc->Packet(buf.read_peek(), buf.size());

			return;
		}
		case SEND_GOLD:
		{
			uGoldType gold = *(uGoldType*)data;

			if (ch)
			{
				m_gold -= gold;

				ch->PointChange(POINT_GOLD, gold, false);
				ch->ChatPacket(CHAT_TYPE_INFO, LC_TEXT("[LC]686"), gold);

				return;
			}

			m_gold -= gold;

			pack.subHeader = SUBHEADER_GG_OFFLINE_SEND_GOLD;

			buf.write(&pack, sizeof(TPacketGGOfflineShop));
			buf.write(&gold, sizeof(uGoldType));

			pcci->pkDesc->Packet(buf.read_peek(), buf.size());
			
			return;
		}
		case SEND_ITEM:
		{
			LPITEM pkItem = (LPITEM)data;

			if (ch)
			{
				ch->AutoGiveItem(pkItem, true);
				return;
			}

			pack.subHeader = SUBHEADER_GG_OFFLINE_SEND_ITEM;

			TPlayerItem item;
			pkItem->CopyToRawData(&item);
			M2_DESTROY_ITEM(pkItem);

			buf.write(&pack, sizeof(TPacketGGOfflineShop));
			buf.write(&item, sizeof(TPlayerItem));

			pcci->pkDesc->Packet(buf.read_peek(), buf.size());

			return;
		}
	}
}

void CShop::Save(bool checkIfEmpty)
{
	if (!m_isOfflineShop)
		return;

	m_table.gold = m_gold;

	if (checkIfEmpty)
	{
		if (IsEmpty())
		{
			if (!m_gold)
				m_table.byChannel = 0;
			else if (!m_bIsLocked)
			{
				m_bIsLocked = true;

				for (GuestMapType::iterator it = m_map_guest.begin(); it != m_map_guest.end();)
				{
					LPCHARACTER ch2 = it->first;

					if (ch2->GetPlayerID() != m_dwOwnerID)
					{
						if (ch2->GetShop() != this)
							return;

						it = m_map_guest.erase(it);
						ch2->SetShop(NULL);

						TPacketGCShop pack;

						pack.header = HEADER_GC_SHOP;
						pack.subheader = SHOP_SUBHEADER_GC_END;
						pack.size = sizeof(TPacketGCShop);

						ch2->GetDesc()->Packet(&pack, sizeof(pack));
					}
					else
						++it;
				}

				m_pkPC->SetOnlyView(m_dwOwnerID);
				BroadcastPacket(SET_LOCK, &m_bIsLocked);
			}
		}
	}

	m_table.bLocked = m_bIsLocked;

	db_clientdesc->DBPacketHeader(HEADER_GD_UPDATE_OFFLINE_SHOP, 0, sizeof(TOfflineShopTable));
	db_clientdesc->Packet(&m_table, sizeof(TOfflineShopTable));

	BroadcastPacket(UPDATE_TABLE, NULL);

	if (!m_table.byChannel)
		M2_DESTROY_CHARACTER(m_pkPC);
}

void CShop::ClearItemPos(BYTE pos, BYTE size)
{
	if (!m_isOfflineShop)
		return;
	
	m_pGrid->Get(pos, 1, size);
	memset(&m_itemVector[pos], 0, sizeof(m_itemVector[pos]));
	m_table.items[pos].vnum = 0;
	m_table.items[pos].price = 0;
	
	BroadcastUpdateItem(pos);
	
	Save(true);
}
#endif
