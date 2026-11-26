#include "KnowledgeBase.h"
#include <cstdint>
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/FrameScript/FrameScript_Object.h"
#include "WoW/Source/Ui/CKBSystem.h"
#include "Engine/Source/Base/DynamicString.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Ui/CKBQuery.h"
#include "WoW/Source/Ui/CKBArticle.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/FrameXML/XMLTree.h"
#include "Engine/Source/Storm/H/storm.h"

// VERIFY: 9e42057a auto
// OFFSET: 0x00551FB0
static int32_t Script_KBSystem_GetMOTD(lua_State* L) {
    lua_pushstring(L, s_motd.m_buffer);
    return 1;
}

// VERIFY: 813271c5 auto
// OFFSET: 0x00551FD0
static int32_t Script_KBSystem_GetServerStatus(lua_State* L) {
    lua_pushstring(L, s_serverStatus.m_buffer);
    return 1;
}

// VERIFY: 88390c3b auto
// OFFSET: 0x00551FF0
static int32_t Script_KBSystem_GetServerNotice(lua_State* L) {
    lua_pushstring(L, s_serverNotice.m_buffer);
    return 1;
}

// VERIFY: 64710f8f auto
// OFFSET: 0x00551720
static int32_t Script_KBSetup_BeginLoading(lua_State* L) {
    int32_t numArticles = static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 1)));
    int32_t pageNumber = static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 2)));
    g_KnowledgeBaseSetup.BeginLoading(numArticles, pageNumber);
    return 0;
}

// VERIFY: e0016275 auto
// OFFSET: 0x00551760
static int32_t Script_KBSetup_IsLoaded(lua_State* L) {
    lua_pushboolean(L, g_KnowledgeBaseSetup.m_state == 2);
    return 1;
}

// VERIFY: bc5e561c auto
// OFFSET: 0x00551B90
static int32_t Script_KBQuery_IsLoaded(lua_State* L) {
    lua_pushboolean(L, g_KnowledgeBaseQuery.m_state == 2);
    return 1;
}

// VERIFY: 1d0a0bbd auto
// OFFSET: 0x00551DA0
static int32_t Script_KBArticle_IsLoaded(lua_State* L) {
    lua_pushboolean(L, g_KnowledgeBaseArticle.m_state == 2);
    return 1;
}

// VERIFY: 3bb58cb3 auto
// OFFSET: 0x00551790
static int32_t Script_KBSetup_GetLanguageCount(lua_State* L) {
    if (g_KnowledgeBaseSetup.m_state != 2) {
        return luaL_error(L, "KBSetup_GetLanguageCount() failed because setup is not loaded");
    }

    lua_pushnumber(L, g_KnowledgeBaseSetup.m_numLanguages);
    return 1;
}

// VERIFY: 453300cc auto
// OFFSET: 0x005518C0
static int32_t Script_KBSetup_GetCategoryCount(lua_State* L) {
    if (g_KnowledgeBaseSetup.m_state != 2) {
        return luaL_error(L, "KBSetup_GetCategoryCount() failed because setup is not loaded");
    }

    lua_pushnumber(L, g_KnowledgeBaseSetup.m_numCategories);
    return 1;
}

// VERIFY: 039b02dc auto
// OFFSET: 0x005519F0
static int32_t Script_KBSetup_GetArticleHeaderCount(lua_State* L) {
    if (g_KnowledgeBaseSetup.m_state != 2) {
        return luaL_error(L, "KBSetup_GetArticleHeaderCount() failed because setup is not loaded");
    }

    lua_pushnumber(L, g_KnowledgeBaseSetup.m_numArticleHeaders);
    return 1;
}

// VERIFY: 516cbb9d auto
// OFFSET: 0x00551BC0
static int32_t Script_KBQuery_GetArticleHeaderCount(lua_State* L) {
    if (g_KnowledgeBaseQuery.m_state != 2) {
        return luaL_error(L, "KBQuery_GetArticleHeaderCount() failed because query is not loaded");
    }

    lua_pushnumber(L, g_KnowledgeBaseQuery.m_numArticleHeaders);
    return 1;
}

// VERIFY: 511f6008 manual
// OFFSET: 0x005517D0
static int32_t Script_KBSetup_GetLanguageData(lua_State* L) {
    if (g_KnowledgeBaseSetup.m_state != 2) {
        luaL_error(L, "KBSetup_GetLanguageData() failed because setup is not loaded");
        return 0;
    }

    XMLNode* node = g_KnowledgeBaseSetup.GetLanguage(static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 1))) - 1);

    if (!node) {
        luaL_error(L, "KBSetup_GetLanguageData() called with invalid category index");
        return 0;
    }

    lua_pushnumber(L, g_KnowledgeBaseSetup._GetInt(node, "id"));
    lua_pushstring(L, g_KnowledgeBaseSetup.m_state == 2 && g_KnowledgeBaseSetup.m_tree ? node->GetAttributeByName("caption") : nullptr);
    return 2;
}

// VERIFY: f63ffcdf manual
// OFFSET: 0x00551900
static int32_t Script_KBSetup_GetCategoryData(lua_State* L) {
    if (g_KnowledgeBaseSetup.m_state != 2) {
        luaL_error(L, "KBSetup_GetCategoryData() failed because setup is not loaded");
        return 0;
    }

    XMLNode* node = g_KnowledgeBaseSetup.GetCategoryNode(static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 1))) - 1);

    if (!node) {
        luaL_error(L, "KBSetup_GetCategoryData() called with invalid category index");
        return 0;
    }

    lua_pushnumber(L, g_KnowledgeBaseSetup._GetInt(node, "id"));
    lua_pushstring(L, g_KnowledgeBaseSetup.m_state == 2 && g_KnowledgeBaseSetup.m_tree ? node->GetAttributeByName("caption") : nullptr);
    return 2;
}

// VERIFY: 2a3a128f manual
// OFFSET: 0x00551A30
static int32_t Script_KBSetup_GetArticleHeaderData(lua_State* L) {
    if (g_KnowledgeBaseSetup.m_state != 2) {
        luaL_error(L, "KBSetup_GetArticleHeaderData() failed because setup is not loaded");
        return 0;
    }

    XMLNode* node = g_KnowledgeBaseSetup.GetArticleHeader(static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 1))) - 1);

    if (!node) {
        luaL_error(L, "KBSetup_GetArticleHeaderData() called with invalid article header index");
        return 0;
    }

    lua_pushnumber(L, g_KnowledgeBaseSetup._GetInt(node, "id"));
    lua_pushstring(L, g_KnowledgeBaseSetup.m_state == 2 && g_KnowledgeBaseSetup.m_tree ? node->GetAttributeByName("subject") : nullptr);
    lua_pushboolean(L, g_KnowledgeBaseSetup._GetBool(node, "hotIssue"));
    lua_pushboolean(L, g_KnowledgeBaseSetup._GetBool(node, "updated"));
    return 4;
}

// VERIFY: 58c4251a auto
// OFFSET: 0x00551B40
static int32_t Script_KBSetup_GetTotalArticleCount(lua_State* L) {
    if (g_KnowledgeBaseSetup.m_state != 2) {
        luaL_error(L, "KBSetup_GetTotalArticleCount() failed because setup is not loaded");
        return 0;
    }

    lua_pushnumber(L, g_KnowledgeBaseSetup._GetInt("ArticleHeaders", "articleCount"));
    return 1;
}

// VERIFY: 6159c4ce manual
// OFFSET: 0x00551C00
static int32_t Script_KBQuery_GetArticleHeaderData(lua_State* L) {
    if (g_KnowledgeBaseQuery.m_state != 2) {
        luaL_error(L, "KBQuery_GetArticleHeaderData() failed because query is not loaded");
        return 0;
    }

    XMLNode* node = g_KnowledgeBaseQuery.GetArticleHeader(static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 1))) - 1);

    if (!node) {
        luaL_error(L, "KBQuery_GetArticleHeaderData() called with invalid article header index");
        return 0;
    }

    lua_pushnumber(L, g_KnowledgeBaseQuery._GetInt(node, "id"));
    lua_pushstring(L, g_KnowledgeBaseQuery.m_state == 2 && g_KnowledgeBaseQuery.m_tree ? node->GetAttributeByName("subject") : nullptr);
    lua_pushboolean(L, g_KnowledgeBaseQuery._GetBool(node, "hotIssue"));
    lua_pushboolean(L, g_KnowledgeBaseQuery._GetBool(node, "updated"));
    return 4;
}

// VERIFY: 73c4bd52 auto
// OFFSET: 0x00551D10
static int32_t Script_KBQuery_GetTotalArticleCount(lua_State* L) {
    if (g_KnowledgeBaseQuery.m_state != 2) {
        luaL_error(L, "KBQuery_GetTotalArticleCount() failed because query is not loaded");
        return 0;
    }

    lua_pushnumber(L, g_KnowledgeBaseQuery._GetInt("ArticleHeaders", "articleCount"));
    return 1;
}

// VERIFY: f4fc784c auto
// OFFSET: 0x00551D60
static int32_t Script_KBArticle_BeginLoading(lua_State* L) {
    int32_t articleId = static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 1)));
    int32_t searchType = static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 2)));
    g_KnowledgeBaseArticle.BeginLoading(articleId, searchType);
    return 0;
}

// VERIFY: 0075add2 manual
// OFFSET: 0x00551DD0
static int32_t Script_KBArticle_GetData(lua_State* L) {
    if (g_KnowledgeBaseArticle.m_state != 2) {
        luaL_error(L, "KBArticle_GetData() failed because article is not loaded");
        return 0;
    }

    lua_pushnumber(L, g_KnowledgeBaseArticle._GetBodyInt("Article", "id"));
    lua_pushstring(L, g_KnowledgeBaseArticle._GetBodyString("Article", "subject"));
    lua_pushstring(L, g_KnowledgeBaseArticle._GetBodyString("Article", "subjectAlt"));
    lua_pushstring(L, g_KnowledgeBaseArticle._GetBodyString("Article", "ArticleText"));
    lua_pushstring(L, g_KnowledgeBaseArticle._GetBodyString("Article", "keyword"));
    lua_pushnumber(L, g_KnowledgeBaseArticle._GetBodyInt("Article", "languageId"));
    lua_pushboolean(L, g_KnowledgeBaseArticle._GetBodyBool("Article", "hotIssue"));
    return 7;
}

// VERIFY: 52b22198 manual
// OFFSET: 0x005523A0
static int32_t Script_KBSetup_GetSubCategoryCount(lua_State* L) {
    if (g_KnowledgeBaseSetup.m_state != 2) {
        luaL_error(L, "KBSetup_GetSubCategoryCount() failed because setup is not loaded");
        return 0;
    }

    double category = lua_tonumber(L, 1);
    int32_t count;

    if (g_KnowledgeBaseSetup.m_currentCategory == category - 1.0) {
        count = g_KnowledgeBaseSetup.m_numSubCategories;
    } else if (g_KnowledgeBaseSetup.m_state == 2 && g_KnowledgeBaseSetup.m_tree) {
        g_KnowledgeBaseSetup.GetCategory(static_cast<int32_t>(category - 1.0));
        count = g_KnowledgeBaseSetup.m_numSubCategories;
    } else {
        count = 0;
    }

    lua_pushnumber(L, count);
    return 1;
}

// VERIFY: cc04f165 manual
// OFFSET: 0x00552440
static int32_t Script_KBSetup_GetSubCategoryData(lua_State* L) {
    if (g_KnowledgeBaseSetup.m_state != 2) {
        luaL_error(L, "KBSetup_GetSubCategoryData() failed because setup is not loaded");
        return 0;
    }

    int32_t subCategory = static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 2))) - 1;
    int32_t category = static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 1) - 1.0));
    XMLNode* node = g_KnowledgeBaseSetup.GetSubCategory(category, subCategory);

    if (!node) {
        luaL_error(L, "KBSetup_GetSubCategoryData() called with invalid category or sub category index");
        return 0;
    }

    lua_pushnumber(L, g_KnowledgeBaseSetup._GetInt(node, "id"));
    lua_pushstring(L, g_KnowledgeBaseSetup.m_state == 2 && g_KnowledgeBaseSetup.m_tree ? node->GetAttributeByName("caption") : nullptr);
    return 2;
}

// VERIFY: f0e5209c manual
// OFFSET: 0x00552580
static int32_t Script_KBQuery_BeginLoading(lua_State* L) {
    if (g_KnowledgeBaseSetup.m_state != 2) {
        luaL_error(L, "KBQuery_BeginLoading() failed because setup is not loaded");
        return 0;
    }

    const char* query = lua_tolstring(L, 1, nullptr);

    if (!query) {
        luaL_error(L, "KBQuery_BeginLoading() called with a null string for search query");
        return 0;
    }

    if (SStrLen(query) > 128) {
        luaL_error(L, "KBQuery_BeginLoading() called with a string > 128 bytes for search query");
        return 0;
    }

    int32_t category = static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 2)));
    int32_t subCategory = static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 3)));
    int32_t numArticles = static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 4)));
    int32_t pageNumber = static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 5)));
    int32_t categoryId = -1;

    if (subCategory || category) {
        XMLNode* node;

        if (subCategory) {
            if (!category) {
                luaL_error(L, "KBQuery_BeginLoading() called with subcategory without category");
                return 0;
            }

            node = g_KnowledgeBaseSetup.GetSubCategory(category - 1, subCategory - 1);
        } else {
            node = g_KnowledgeBaseSetup.GetCategoryNode(category - 1);
        }

        if (!node) {
            luaL_error(L, "KBSetup_GetCategoryData() called with invalid category index");
            return 0;
        }

        categoryId = g_KnowledgeBaseSetup._GetInt(node, "id");
    }

    g_KnowledgeBaseQuery.BeginLoading(query, categoryId, numArticles, pageNumber);
    return 0;
}

// STATIC: 0x00ACD488
static FrameScript_Method s_apMethodMap[] = {
    { "KBSetup_BeginLoading", &Script_KBSetup_BeginLoading },
    { "KBSetup_IsLoaded", &Script_KBSetup_IsLoaded },
    { "KBSetup_GetLanguageCount", &Script_KBSetup_GetLanguageCount },
    { "KBSetup_GetLanguageData", &Script_KBSetup_GetLanguageData },
    { "KBSetup_GetCategoryCount", &Script_KBSetup_GetCategoryCount },
    { "KBSetup_GetCategoryData", &Script_KBSetup_GetCategoryData },
    { "KBSetup_GetSubCategoryCount", &Script_KBSetup_GetSubCategoryCount },
    { "KBSetup_GetSubCategoryData", &Script_KBSetup_GetSubCategoryData },
    { "KBSetup_GetArticleHeaderCount", &Script_KBSetup_GetArticleHeaderCount },
    { "KBSetup_GetArticleHeaderData", &Script_KBSetup_GetArticleHeaderData },
    { "KBSetup_GetTotalArticleCount", &Script_KBSetup_GetTotalArticleCount },
    { "KBQuery_BeginLoading", &Script_KBQuery_BeginLoading },
    { "KBQuery_IsLoaded", &Script_KBQuery_IsLoaded },
    { "KBQuery_GetArticleHeaderCount", &Script_KBQuery_GetArticleHeaderCount },
    { "KBQuery_GetArticleHeaderData", &Script_KBQuery_GetArticleHeaderData },
    { "KBQuery_GetTotalArticleCount", &Script_KBQuery_GetTotalArticleCount },
    { "KBArticle_BeginLoading", &Script_KBArticle_BeginLoading },
    { "KBArticle_IsLoaded", &Script_KBArticle_IsLoaded },
    { "KBArticle_GetData", &Script_KBArticle_GetData },
    { "KBSystem_GetMOTD", &Script_KBSystem_GetMOTD },
    { "KBSystem_GetServerStatus", &Script_KBSystem_GetServerStatus },
    { "KBSystem_GetServerNotice", &Script_KBSystem_GetServerNotice },
};

// VERIFY: a3329701 auto
// OFFSET: 0x00552010
void KnowledgeBase_RegisterScriptFunctions() {
    for (uint32_t i = 0; i < sizeof(s_apMethodMap) / sizeof(s_apMethodMap[0]); i++) {
        FrameScript_RegisterFunction(s_apMethodMap[i].m_name, s_apMethodMap[i].m_method);
    }
}

// VERIFY: 25924890 auto
// OFFSET: 0x00552040
void KnowledgeBase_UnregisterScriptFunctions() {
    for (uint32_t i = 0; i < sizeof(s_apMethodMap) / sizeof(s_apMethodMap[0]); i++) {
        FrameScript_UnregisterFunction(s_apMethodMap[i].m_name);
    }
}
