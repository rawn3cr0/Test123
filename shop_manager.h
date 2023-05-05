#ifndef __INC_METIN_II_GAME_SHOP_MANAGER_H__
#define __INC_METIN_II_GAME_SHOP_MANAGER_H__

#if defined(BL_PRIVATESHOP_SEARCH_SYSTEM)
#include "packet.h"
#endif


class CShop;
typedef class CShop * LPSHOP;

class CShopManager : public singleton<CShopManager>
{
public:
	typedef std::map<DWORD, CShop *> TShopMap;

public:
	CShopManager();
	virtual ~CShopManager();

	bool	Initialize(TShopTable * table, int size);
	void	Destroy();

	LPSHOP	Get(DWORD dwVnum);
	LPSHOP	GetByNPCVnum(DWORD dwVnum);

	bool	StartShopping(LPCHARACTER pkChr, LPCHARACTER pkShopKeeper, int iShopVnum = 0);
	void	StopShopping(LPCHARACTER ch);

	void	Buy(LPCHARACTER ch, BYTE pos);
	void	Sell(LPCHARACTER ch, BYTE bCell, BYTE bCount=0);
	LPSHOP	CreatePCShop(LPCHARACTER ch, TShopItemTable * pTable, BYTE bItemCount);
	LPSHOP	FindPCShop(DWORD dwVID);
	void	DestroyPCShop(LPCHARACTER ch);
#ifdef ENABLE_OFFLINE_SHOP
	void	CreateOfflineShop(TOfflineShopTable *table);
	void	CloseOfflineShop(LPCHARACTER ch);
	
	LPSHOP	FindOfflineShop(DWORD dwPID);
	
	void	LockOfflineShop(DWORD dwPID, bool bLock, bool broadcast = true);
	void	ChangeSign(DWORD dwPID, const char* sign, bool broadcast = true);
	
	void	WithdrawGold(DWORD dwPID, uGoldType gold, bool broadcast = true);
	void	WithdrawItem(DWORD dwPID, BYTE pos, bool broadcast = true);
	void	AddItem(DWORD dwPID, TPlayerItem* item, bool broadcast = true);
	bool	LockStatus(DWORD dwPID);
#endif

#if defined(BL_PRIVATESHOP_SEARCH_SYSTEM)
	void ShopSearchBuy(LPCHARACTER ch, const TPacketCGPrivateShopSearchBuyItem* p);
	void ShopSearchProcess(LPCHARACTER ch, const TPacketCGPrivateShopSearch* p);
#endif

private:
	TShopMap	m_map_pkShop;
	TShopMap	m_map_pkShopByNPCVnum;
	TShopMap	m_map_pkShopByPC;
#ifdef ENABLE_OFFLINE_SHOP
	TShopMap	m_map_pkOfflineShop;
#endif

	bool	ReadShopTableEx(const char* stFileName);
};

#endif

