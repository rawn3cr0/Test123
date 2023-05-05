#ifndef __INC_METIN_II_GAME_SHOP_H__
#define __INC_METIN_II_GAME_SHOP_H__

enum
{
	SHOP_MAX_DISTANCE = 1000,
#ifdef ENABLE_OFFLINE_SHOP
	UPDATE_TABLE = 0,
	SET_LOCK,
	UPDATE_TIME,
	UPDATE_SIGN,
	UPDATE_GOLD,
	SEND_GOLD,
	SEND_ITEM,
#endif
};

class CGrid;

/* ---------------------------------------------------------------------------------- */
class CShop
{
	public:
		typedef struct shop_item
		{
			DWORD	vnum;		// 아이템 번호
#ifdef ENABLE_YANG_LIMIT
			long long	price;
#else
			long	price;		// 가격
#endif
			BYTE	count;		// 아이템 개수
			LPITEM	pkItem;
			int		itemid;		// 아이템 고유아이디

#ifdef ENABLE_OFFLINE_SHOP
			shop_item(DWORD dwVnum = 0, uGoldType Price = 0, BYTE bCount = 0, LPITEM pItem = NULL, int itemID = 0)
				: vnum(dwVnum), price(Price), count(bCount), pkItem(pItem), itemid(itemID) {}
#else
			shop_item()
			{
				vnum = 0;
				price = 0;
				count = 0;
				itemid = 0;
				pkItem = 0;
			}
#endif
		} SHOP_ITEM;

		CShop();
		virtual ~CShop(); // @fixme139 (+virtual)

#ifdef ENABLE_OFFLINE_SHOP
		CShop(TOfflineShopTable	*pTable);
#endif

		bool	Create(DWORD dwVnum, DWORD dwNPCVnum, TShopItemTable * pItemTable);
		void	SetShopItems(TShopItemTable * pItemTable, BYTE bItemCount);

		virtual void	SetPCShop(LPCHARACTER ch);
		virtual bool	IsPCShop()	{ return m_pkPC ? true : false; }

		// 게스트 추가/삭제
		virtual bool	AddGuest(LPCHARACTER ch,DWORD owner_vid, bool bOtherEmpire);
		void	RemoveGuest(LPCHARACTER ch);

#if defined(BL_PRIVATESHOP_SEARCH_SYSTEM)
		const std::vector<SHOP_ITEM>& GetItemVector() const { return m_itemVector; }
		LPCHARACTER GetShopOwner() { return m_pkPC; }
#endif

		// 물건 구입
#ifdef ENABLE_YANG_LIMIT
		virtual long long	Buy(LPCHARACTER ch, BYTE pos, bool bIsShopSearch = false);
#else
		virtual int	Buy(LPCHARACTER ch, BYTE pos, bool bIsShopSearch = false);
#endif
		// 게스트에게 패킷을 보냄
		void	BroadcastUpdateItem(BYTE pos);

		// 판매중인 아이템의 갯수를 알려준다.
		int		GetNumberByVnum(DWORD dwVnum);

		// 아이템이 상점에 등록되어 있는지 알려준다.
		virtual bool	IsSellingItem(DWORD itemID);

		DWORD	GetVnum() { return m_dwVnum; }
		DWORD	GetNPCVnum() { return m_dwNPCVnum; }

#ifdef ENABLE_OFFLINE_SHOP
		void	OnClose(LPCHARACTER ch);
		void	SetLock(bool bLock);
		void	ChangeSign(const char *sign);
		void	WithdrawGold(uGoldType gold);
		void	WithdrawItem(BYTE pos);
		void	AddItem(TPlayerItem *table);
		void	BroadcastPacket(BYTE type, const void *data);
		void	Save(bool checkIfEmpty = false);
		void	ClearItemPos(BYTE pos, BYTE size);
		bool	IsEmpty();
		bool	LockStatus() { return m_bIsLocked; }
#endif

	protected:
		void	Broadcast(const void * data, int bytes);

	protected:
		DWORD				m_dwVnum;
		DWORD				m_dwNPCVnum;

		CGrid *				m_pGrid;

		typedef TR1_NS::unordered_map<LPCHARACTER, bool> GuestMapType;
		GuestMapType m_map_guest;
		std::vector<SHOP_ITEM>		m_itemVector;	// 이 상점에서 취급하는 물건들

		LPCHARACTER			m_pkPC;

#ifdef ENABLE_OFFLINE_SHOP
		TOfflineShopTable	m_table;
		bool				m_isOfflineShop;
		DWORD				m_dwOwnerID;
		bool				m_bIsLocked;
		uGoldType			m_gold;
#endif

};

#endif 
