Esteria garrison handoff, 16 November 2026. 91 MB, 810 files.
Unrelated MPQ archives from tools\_mpq_src (Kyrian, Ethereal, DragonUI and the rest, about 6 GB) were left out.

Where to put it
---------------
client\Interface\AddOns\EsteriaGarrison
    into the client at Interface\AddOns\EsteriaGarrison
    This is the live addon: the architect window and missions. Do not use the copy under wowdecomp-github\Addons. That one is older.

client\Interface\AddOns\Blizzard_GarrisonUI
    into the client at Interface\AddOns\Blizzard_GarrisonUI
    Window art. The toc is named Blizzard_GarrisonUI.toc.bak on purpose.
    Do not rename it to .toc. That would load the old architect window over the current one.

client\Data\Patch-ZZZZZZZZZZZZZZZZZZZZZZZZZZGarrison.MPQ
    into the client Data\ folder.
    This is the archive mounted by the client that is running now (4 MB).

server\modules\mod-esteria-garrison
    into the server sources at modules\mod-esteria-garrison
    Rebuild worldserver after that.

server\etc\modules\esteria_garrison.conf
    into etc\modules\ of the running server.
    This copy has EsteriaGarrison.Enable = 1.
    conf\esteria_garrison.conf.dist inside the module defaults to 0.

server\core\game\Handlers\EsteriaGarrisonHooks.h
server\core\game\Handlers\EsteriaGarrisonHandler.cpp
    into src\server\game\Handlers\
    These are the only new core files. The rest is three edits in files that already exist, listed below.

client-source\Extensions\Garrison\src\Garrison.cpp
    into the client sources at Extensions\Garrison\src\
    Only needed if he builds Wow.exe himself. A built Esteria client already has this code inside the exe.

Core edits
----------
src\server\game\Server\Protocol\Opcodes.h, next to the other Esteria opcodes:
    CMSG_ESTERIA_GARRISON_HELLO  = 0x54A
    SMSG_ESTERIA_GARRISON_STATE  = 0x54B
    CMSG_ESTERIA_GARRISON_ACTION = 0x54C

src\server\game\Server\Protocol\Opcodes.cpp, at the end of the handler table:
    DEFINE_HANDLER(CMSG_ESTERIA_GARRISON_HELLO, STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleEsteriaGarrisonOpcode);
    DEFINE_SERVER_OPCODE_HANDLER(SMSG_ESTERIA_GARRISON_STATE, STATUS_NEVER);
    DEFINE_HANDLER(CMSG_ESTERIA_GARRISON_ACTION, STATUS_LOGGEDIN, PROCESS_THREADUNSAFE, &WorldSession::HandleEsteriaGarrisonOpcode);

src\server\game\Server\WorldSession.h:
    void HandleEsteriaGarrisonOpcode(WorldPacket& recvPacket);

src\server\game\Maps\MapInstanced.cpp:
    #include "EsteriaGarrisonHooks.h"
    and the owner-instance branch on EsteriaGarrisonHooks::Instance,
    immediately after the matching housing branch.

SQL
---
On the characters database:
    server\modules\mod-esteria-garrison\sql\characters\garrison_characters.sql

On the world database, in this order:
    data\sql\db-world\garrison_db2.sql
    sql\world\garrison_world.sql
    data\sql\db-world\garrison_uwow_world.sql
    then the remaining sql\world\*.sql files

The live world already has these applied. Running them again recreates the garrison tables and spawns.

Not in this folder
------------------
Terrain for maps 1158, 1331, 1159, 1152, 1330 and 1153. The client loads it from the retail cache
(a WDT alias on those Map.dbc rows). Without it the architect window still opens, but entering the garrison has no ground.
The retail cache was not copied. It is the shared client cache, not a garrison file.
