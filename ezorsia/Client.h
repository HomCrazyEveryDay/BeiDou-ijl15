#pragma once
class Client
{
public:
	static void UpdateGameStartup();
	static void EnableNewIGCipher();
	static void UpdateResolution();
	static void UpdateLogin();
	static void FixMouseWheel();
	static void Chinese();
	static void LongQuickSlot();
	static void MovementKeyRebind();
	static void FixDateFormat();
	static void FixItemType();
	static void JumpCap();
	static void FixChatPosHook();
	static void NoPassword();
	static void MoreHook();
	static void WorldMap();
	static void RefreshRate();
	static void DeleteChar();
	static const int m_nIGCipherHash = 0XC65053F2;
	static int m_nGameHeight;
	static int m_nGameWidth;
	static int MsgAmount;
	static bool CustomLoginFrame;
	static bool WindowedMode;
	static bool RemoveLogos;
	static int setDamageCap;
	static int setMAtkCap;
	static int setAccCap;
	static int setAvdCap;
	static double setAtkOutCap;
	static double setSnipeAtkOutCap;
	static bool useTubi;
	static bool bigLoginFrame;
	static bool SwitchChinese;
	static bool debug;
	static bool noPassword;
	static bool climbSpeedAuto;
	static float climbSpeed;
	static int speedMovementCap;
	static unsigned char imeType;
	static DWORD jumpCap;
	static std::string ServerIP_Address;
	static int serverIP_Port;
	static bool talkRepeat;
	static int talkTime;
	static bool enableMovementKeyRebind;
	static bool forceAccountShareTooltipLine;
	static bool enableStartupLog;
};

__declspec(selectany) DWORD g_facePreviewFaceId = 0;
__declspec(selectany) DWORD g_facePreviewFaceId2 = 0;
__declspec(selectany) DWORD g_facePreviewFaceId3 = 0;
