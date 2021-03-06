// Copyright 2016 Björn Ritzl

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <extension/extension.h>
#include <script/script.h>  // DM_LUA_STACK_CHECK, CheckHashOrString, PushBuffer, Ref, Unref"
#include <dlib/log.h>
#include "./steam_api.h"


#define LIB_NAME "steamworks"
#define MODULE_NAME "steamworks"

#define DLIB_LOG_DOMAIN LIB_NAME

#define DM_STEAMWORKS_EXTENSION_STAT_TYPE_INT 0
#define DM_STEAMWORKS_EXTENSION_STAT_TYPE_FLOAT 1
#define DM_STEAMWORKS_EXTENSION_STAT_TYPE_AVERAGERATE 2

static ISteamFriends *steamFriends;
static ISteamUser *steamUser;
static ISteamUserStats *steamUserStats;


struct SteamworksListener {
  SteamworksListener() {
    m_L = 0;
    m_Callback = LUA_NOREF;
    m_Self = LUA_NOREF;
  }
  lua_State* m_L;
  int        m_Callback;
  int        m_Self;
};

static SteamworksListener steamworksListener;

static int VerifyListener() {
  if (steamworksListener.m_Callback == LUA_NOREF) {
    return 0;
  }

  lua_State* L = steamworksListener.m_L;
  int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, steamworksListener.m_Callback);

  // Setup self
  lua_rawgeti(L, LUA_REGISTRYINDEX, steamworksListener.m_Self);
  lua_pushvalue(L, -1);
  dmScript::SetInstance(L);

  if (!dmScript::IsInstanceValid(L)) {
    dmLogError("Could not run Steamworks callback because the instance has been deleted.");
    lua_pop(L, 2);
    assert(top == lua_gettop(L));
    return 0;
  }
  return 1;
}

static void NotifyListener(const char* event) {
  lua_State* L = steamworksListener.m_L;
  int top = lua_gettop(L);

  if (!VerifyListener()) {
    return;
  }

  lua_pushstring(L, event);

  int ret = lua_pcall(L, 2, LUA_MULTRET, 0);
  if (ret != 0) {
    dmLogError("Error running Steamworks callback: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  assert(top == lua_gettop(L));
}

static void NotifyListener(const char* event, LeaderboardFindResult_t *result) {
  lua_State* L = steamworksListener.m_L;
  int top = lua_gettop(L);

  if (!VerifyListener()) {
    return;
  }

  lua_pushstring(L, event);

  lua_newtable(L);

  lua_pushstring(L, steamUserStats->GetLeaderboardName(result->m_hSteamLeaderboard));
  lua_setfield(L, -2, "leaderboardName");

  lua_pushnumber(L, steamUserStats->GetLeaderboardEntryCount(result->m_hSteamLeaderboard));
  lua_setfield(L, -2, "entryCount");

  int ret = lua_pcall(L, 3, LUA_MULTRET, 0);
  if (ret != 0) {
    dmLogError("Error running Steamworks callback: %s", lua_tostring(L, -1));
    lua_pop(L, 2);
  }
  assert(top == lua_gettop(L));
}

static int SetListener(lua_State* L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_pushvalue(L, 1);
  int cb = dmScript::Ref(L, LUA_REGISTRYINDEX);

  if (steamworksListener.m_Callback != LUA_NOREF) {
    dmScript::Unref(steamworksListener.m_L, LUA_REGISTRYINDEX, steamworksListener.m_Callback);
    dmScript::Unref(steamworksListener.m_L, LUA_REGISTRYINDEX, steamworksListener.m_Self);
  }

  steamworksListener.m_L = dmScript::GetMainThread(L);
  steamworksListener.m_Callback = cb;
  dmScript::GetInstance(L);
  steamworksListener.m_Self = dmScript::Ref(L, LUA_REGISTRYINDEX);
  return 0;
}





class SteamCallbackWrapper {
 public:
    SteamCallbackWrapper();
    STEAM_CALLBACK(SteamCallbackWrapper, OnUserStatsReceived, UserStatsReceived_t, m_CallbackUserStatsReceived);
    STEAM_CALLBACK(SteamCallbackWrapper, OnUserStatsStored, UserStatsStored_t, m_CallbackUserStatsStored);
    STEAM_CALLBACK(SteamCallbackWrapper, OnAchievementStored, UserAchievementStored_t, m_CallbackAchievementStored);
    STEAM_CALLBACK(SteamCallbackWrapper, OnPS3TrophiesInstalled, PS3TrophiesInstalled_t, m_CallbackPS3TrophiesInstalled);

    void OnFindLeaderboard(LeaderboardFindResult_t *pFindLearderboardResult, bool bIOFailure);
    CCallResult<SteamCallbackWrapper, LeaderboardFindResult_t> m_SteamCallResultCreateLeaderboard;
};

SteamCallbackWrapper::SteamCallbackWrapper()
  :
  m_CallbackUserStatsReceived(this, &SteamCallbackWrapper::OnUserStatsReceived),
  m_CallbackUserStatsStored(this, &SteamCallbackWrapper::OnUserStatsStored),
  m_CallbackAchievementStored(this, &SteamCallbackWrapper::OnAchievementStored),
  m_CallbackPS3TrophiesInstalled(this, &SteamCallbackWrapper::OnPS3TrophiesInstalled) {
}

void SteamCallbackWrapper::OnUserStatsReceived(UserStatsReceived_t *pCallback) {
  printf("SteamCallbackWrapper::OnUserStatsReceived\n");
  NotifyListener("OnUserStatsReceived");
}
void SteamCallbackWrapper::OnUserStatsStored(UserStatsStored_t *pCallback) {
  printf("SteamCallbackWrapper::OnUserStatsStored\n");
  NotifyListener("OnUserStatsStored");
}
void SteamCallbackWrapper::OnAchievementStored(UserAchievementStored_t *pCallback) {
  printf("SteamCallbackWrapper::OnAchievementStored\n");
  NotifyListener("OnAchievementStored");
}
void SteamCallbackWrapper::OnPS3TrophiesInstalled(PS3TrophiesInstalled_t *pCallback) {
  printf("SteamCallbackWrapper::OnPS3TrophiesInstalled\n");
  NotifyListener("OnPS3TrophiesInstalled");
}
void SteamCallbackWrapper::OnFindLeaderboard(LeaderboardFindResult_t *pFindLeaderboardResult, bool bIOFailure) {
  printf("SteamCallbackWrapper::OnFindLeaderboard\n");
  NotifyListener("OnFindLeaderboard", pFindLeaderboardResult);
}

static SteamCallbackWrapper *steamCallbackWrapper = new SteamCallbackWrapper();

/**
 * Push a CSteamID on the stack
 * The 64 bit representation of the steam id will be
 * converted into a string
 */
static int PushSteamID(lua_State* L, CSteamID steamId) {
  char buf[22];
  snprintf(buf, sizeof(buf), "%llu", steamId.ConvertToUint64());
  lua_pushstring(L, buf);
  return 1;
}

/**
 * Get a CSteamID from the stack
 */
static CSteamID* GetSteamID(lua_State* L, int index) {
  char * pEnd;
  const char * s = lua_tostring(L, index);
  uint64 id = strtoull(s, &pEnd, 10);
  CSteamID *steamId = new CSteamID(id);
  return steamId;
}

static int IsLoggedInUserId(lua_State* L, int index) {
  printf("IsLoggedInUserId %d\n", index);
  if (lua_isnoneornil(L, index)) {
    return 1;
  }
  if (lua_gettop(L) == 0) {
    return 1;
  }
  if (lua_isstring(L, index)) {
    CSteamID* id = GetSteamID(L, index);
    if (steamUser->GetSteamID().ConvertToUint64() == id->ConvertToUint64()) {
      return 1;
    }
  }
  return 0;
}

/**
 * Print the contents of the stack
 */
static void PrintStack(lua_State* L) {
  int n = lua_gettop(L);
  for (int i = 1; i <= n; i++)  {
    printf("STACK %d %s\r\n", i, lua_tostring(L, i));
  }
}


static int Init(lua_State* L) {
  SteamAPI_Init();
  if (!SteamAPI_IsSteamRunning()) {
    luaL_error(L, "Steam is not running");
  }
  steamFriends = SteamFriends();
  steamUser = SteamUser();
  steamUserStats = SteamUserStats();
  steamUserStats->RequestCurrentStats();
  return 1;
}

static int Update(lua_State* L) {
  SteamAPI_RunCallbacks();
  return 0;
}

static int Final(lua_State* L) {
  SteamAPI_Shutdown();
  return 0;
}

static int GetAchievementInfo(lua_State* L) {
  printf("GetAchievementInfo\n");

  if (steamUserStats == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "steamUserStats is nil");
    return 2;
  }

  bool achieved;
  uint32 unlockTime;
  const char* achievementId = lua_tostring(L, lua_gettop(L));
  // steamUserStats->GetAchievement( achievementId, &achieved);
  // lua_pushstring(L, )
  steamUserStats->GetAchievementAndUnlockTime(achievementId, &achieved, &unlockTime);

  int top = lua_gettop(L);

  lua_newtable(L);

  lua_pushboolean(L, achieved);
  lua_setfield(L, -2, "unlocked");

  lua_pushnumber(L, unlockTime);
  lua_setfield(L, -2, "unlockTime");

  // hidden, localizedDescription, localizedName, unlocked, unlockTime

  assert(top + 1 == lua_gettop(L));
  return 1;
}

static int GetAchievementNames(lua_State* L) {
  printf("GetAchievementNames\n");

  if (steamUserStats == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "steamUserStats is nil");
    return 2;
  }

  int top = lua_gettop(L);

  lua_newtable(L);

  uint32 num = steamUserStats->GetNumAchievements();
  for (int i = 0; i < num; i++) {
    const char* name = steamUserStats->GetAchievementName(i);
    lua_pushinteger(L, i);
    lua_pushstring(L, name);
    lua_rawset(L, -3);
  }

  assert(top + 1 == lua_gettop(L));

  return 1;
}

static int GetUserInfo(lua_State* L) {
  if (steamFriends == NULL || steamUser == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "steamFriends or steamUser is nil");
    return 2;
  }

  int top = lua_gettop(L);

  if (IsLoggedInUserId(L, 1)) {
    CSteamID steamId = steamUser->GetSteamID();

    lua_newtable(L);

    PushSteamID(L, steamId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, steamFriends->GetPersonaName());
    lua_setfield(L, -2, "name");

    lua_pushinteger(L, steamUser->GetPlayerSteamLevel());
    lua_setfield(L, -2, "steam_level");
  } else {
    CSteamID *steamId = GetSteamID(L, 1);

    lua_newtable(L);

    PushSteamID(L, *steamId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, steamFriends->GetFriendPersonaName(*steamId));
    lua_setfield(L, -2, "name");

    lua_pushnumber(L, steamFriends->GetFriendSteamLevel(*steamId));
    lua_setfield(L, -2, "steam_level");

    delete steamId;
  }
  assert(top + 1 == lua_gettop(L));
  return 1;
}

static int GetUserStatValue(lua_State* L) {
  if (steamUserStats == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "steamUserStats is nil");
    return 2;
  }

  luaL_checktype(L, 1, LUA_TSTRING);
  const char* statName = lua_tostring(L, 1);

  luaL_checktype(L, 2, LUA_TNUMBER);
  const int type = lua_tointeger(L, 2);

  int32 top = lua_gettop(L);

  switch (type) {
    case DM_STEAMWORKS_EXTENSION_STAT_TYPE_INT:
      int statValueInt;
      steamUserStats->GetStat(statName, &statValueInt);
      lua_pushinteger(L, statValueInt);
      break;
    case DM_STEAMWORKS_EXTENSION_STAT_TYPE_FLOAT:
    case DM_STEAMWORKS_EXTENSION_STAT_TYPE_AVERAGERATE:
      float statValueFloat;
      steamUserStats->GetStat(statName, &statValueFloat);
      lua_pushnumber(L, statValueFloat);
      break;
    default:
      luaL_error(L, "Unknown stat type");
  }

  assert(top + 1 == lua_gettop(L));

  return 1;
}



static int SetUserStatValue(lua_State* L) {
  if (steamUserStats == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "steamUserStats is nil");
    return 2;
  }

  luaL_checktype(L, 1, LUA_TSTRING);
  const char* statName = lua_tostring(L, 1);

  luaL_checktype(L, 2, LUA_TNUMBER);
  const int type = lua_tointeger(L, 2);

  if (type == DM_STEAMWORKS_EXTENSION_STAT_TYPE_INT) {
    luaL_checktype(L, 3, LUA_TNUMBER);
    const int statValueInt = lua_tointeger(L, 3);

    steamUserStats->SetStat(statName, statValueInt);
  } else if (type == DM_STEAMWORKS_EXTENSION_STAT_TYPE_FLOAT) {
    luaL_checktype(L, 3, LUA_TNUMBER);
    const float statValueFloat = lua_tonumber(L, 3);

    steamUserStats->SetStat(statName, statValueFloat);
  } else if (type == DM_STEAMWORKS_EXTENSION_STAT_TYPE_AVERAGERATE) {
    luaL_checktype(L, 3, LUA_TNUMBER);
    const float countThisSession = lua_tonumber(L, 3);

    luaL_checktype(L, 4, LUA_TNUMBER);
    double sessionLength = lua_tonumber(L, 4);

    steamUserStats->UpdateAvgRateStat(statName, countThisSession, sessionLength);
  } else {
    luaL_error(L, "Unknown stat type");
  }

  lua_pushboolean(L, 1);
  return 1;
}

static int ShowGameOverlay(lua_State* L) {
  if (steamFriends == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "steamFriends is nil");
    return 2;
  }

  steamFriends->ActivateGameOverlay("Achievements");

  lua_pushboolean(L, 1);
  return 1;
}


static int RequestLeaderboardInfo(lua_State* L) {
  printf("RequestLeaderboardInfo\n");
  if (steamUserStats == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "steamUserStats is nil");
    return 2;
  }

  SteamAPICall_t hSteamAPICall = steamUserStats->FindOrCreateLeaderboard(
     "Quickest Win",
     k_ELeaderboardSortMethodAscending,
     k_ELeaderboardDisplayTypeTimeSeconds);

  if ( hSteamAPICall != 0 ) {
    printf("hSteamAPICall is NOT 0\n");
    // set the function to call when this API call has completed
    steamCallbackWrapper->m_SteamCallResultCreateLeaderboard.Set(hSteamAPICall, steamCallbackWrapper, &SteamCallbackWrapper::OnFindLeaderboard);
  } else {
    printf("hSteamAPICall is 0\n");
  }
  return 0;
}

static const luaL_reg Module_methods[] = {
    { "init", Init },
    { "update", Update },
    { "final", Final },
    { "get_achievement_info", GetAchievementInfo },
    { "get_achievement_names", GetAchievementNames },
    { "get_user_info", GetUserInfo },
    { "get_user_stat_value", GetUserStatValue },
    { "set_user_stat_value", SetUserStatValue },
    { "request_leaderboard_info", RequestLeaderboardInfo },
    { "set_listener", SetListener },
    //{ "show_game_overlay", ShowGameOverlay },
    { 0, 0 }
};

static void LuaInit(lua_State* L) {
    int top = lua_gettop(L);
    luaL_register(L, MODULE_NAME, Module_methods);

    #define SETCONSTANT(name, val) \
    lua_pushnumber(L, (lua_Number) val); \
    lua_setfield(L, -2, #name);

    SETCONSTANT(STAT_TYPE_INT, DM_STEAMWORKS_EXTENSION_STAT_TYPE_INT);
    SETCONSTANT(STAT_TYPE_FLOAT, DM_STEAMWORKS_EXTENSION_STAT_TYPE_FLOAT);
    SETCONSTANT(STAT_TYPE_AVERAGERATE, DM_STEAMWORKS_EXTENSION_STAT_TYPE_AVERAGERATE);

    #undef SETCONSTANT

    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

dmExtension::Result AppInitializeSteamworks(dmExtension::AppParams* params) {
    return dmExtension::RESULT_OK;
}

dmExtension::Result InitializeSteamworks(dmExtension::Params* params) {
    LuaInit(params->m_L);
    printf("Registered %s Extension\n", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

dmExtension::Result AppFinalizeSteamworks(dmExtension::AppParams* params) {
    return dmExtension::RESULT_OK;
}

dmExtension::Result FinalizeSteamworks(dmExtension::Params* params) {
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(steamworks, LIB_NAME, AppInitializeSteamworks, AppFinalizeSteamworks, InitializeSteamworks, 0, 0, FinalizeSteamworks)
