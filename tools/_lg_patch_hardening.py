"""Scratch: byte-exact patch for report hardening (CRLF-safe for core files)."""

def patch(path, old, new, count=1):
    data = open(path, 'rb').read()
    n = data.count(old)
    assert n == count, (path, n)
    open(path, 'wb').write(data.replace(old, new))

# --- CharacterDatabase.h (CRLF): enum entries ---
patch('src/server/database/Database/Implementation/CharacterDatabase.h',
      b'    MAX_CHARACTERDATABASE_STATEMENTS\r\n',
      b'    // LivingGear (modules/mod-living-gear): in-game bug reports.\r\n'
      b'    CHAR_INS_LG_BUG_REPORT,\r\n'
      b'    CHAR_SEL_LG_BUG_REPORT_VERIFY,\r\n'
      b'\r\n'
      b'    MAX_CHARACTERDATABASE_STATEMENTS\r\n')

# --- CharacterDatabase.cpp (CRLF): statement SQL ---
patch('src/server/database/Database/Implementation/CharacterDatabase.cpp',
      b'    PrepareStatement(CHAR_NO_OP_PROVIDE_REALM_CONTEXT, "SELECT ? AS no_op", CONNECTION_ASYNC);\r\n',
      b'    PrepareStatement(CHAR_NO_OP_PROVIDE_REALM_CONTEXT, "SELECT ? AS no_op", CONNECTION_ASYNC);\r\n'
      b'    PrepareStatement(CHAR_INS_LG_BUG_REPORT,\r\n'
      b'        "INSERT INTO lg_bug_report (report_type, is_critical, is_recurring, account_id, character_guid,"\r\n'
      b'        " character_name, reported_at, map_id, zone_id, zone_name, pos_x, pos_y, pos_z, player_level,"\r\n'
      b'        " target_entry, target_name, description) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",\r\n'
      b'        CONNECTION_ASYNC);\r\n'
      b'    PrepareStatement(CHAR_SEL_LG_BUG_REPORT_VERIFY,\r\n'
      b'        "SELECT id FROM lg_bug_report WHERE account_id = ? AND reported_at = ? AND description = ?",\r\n'
      b'        CONNECTION_ASYNC);\r\n')

# --- LivingGear_Support.cpp (LF): readback helpers before RecordSupportReport ---
helpers = (
    b'\n'
    b'// Readback state for one filed report. Kept alive across the async verify by\n'
    b'// shared_ptr, because the player may log out before the query returns -- the\n'
    b'// guid is resolved fresh in the callback instead of holding a Player*.\n'
    b'struct ReportDelivery\n'
    b'{\n'
    b'    ObjectGuid playerGuid;\n'
    b'    uint32 accountId;\n'
    b'    std::string playerName;\n'
    b'    uint32 reportedAt;\n'
    b'    std::string text;\n'
    b'    bool critical;\n'
    b'    uint8 attempts = 0;\n'
    b'};\n'
    b'\n'
    b'// Owns the report readback callbacks. World.cpp ticks its own processor the\n'
    b'// same way every update; this one exists so confirmations do not depend on\n'
    b'// any particular script object staying alive.\n'
    b'QueryCallbackProcessor& ReportQueryProcessor()\n'
    b'{\n'
    b'    static QueryCallbackProcessor processor;\n'
    b'    return processor;\n'
    b'}\n'
    b'\n'
    b'// The insert is fire-and-forget: the database layer logs failures where no\n'
    b'// player will ever see them (that is how every report filed between the\n'
    b'// 2026-08-29 intake redesign and its fix was silently lost while the player\n'
    b'// read "Reported, thank you"). Read the row back before confirming anything;\n'
    b'// if it never shows up, say so in chat and shout in the log so the text can\n'
    b'// be recovered from either place.\n'
    b'void VerifyReportRow(std::shared_ptr<ReportDelivery> state)\n'
    b'{\n'
    b'    CharacterDatabasePreparedStatement* stmt =\n'
    b'        CharacterDatabase.GetPreparedStatement(CHAR_SEL_LG_BUG_REPORT_VERIFY);\n'
    b'    stmt->SetData(0, state->accountId);\n'
    b'    stmt->SetData(1, state->reportedAt);\n'
    b'    stmt->SetData(2, state->text);\n'
    b'    ReportQueryProcessor().AddCallback(\n'
    b'        CharacterDatabase.AsyncQuery(stmt).WithPreparedCallback([state](PreparedQueryResult result)\n'
    b'        {\n'
    b'            if (result)\n'
    b'            {\n'
    b'                if (Player* player = ObjectAccessor::FindPlayer(state->playerGuid))\n'
    b'                    ChatHandler(player->GetSession()).SendSysMessage(\n'
    b'                        state->critical\n'
    b'                            ? "|cffff3333[CRITICAL]|r Reported, thank you. Your location and target were included."\n'
    b'                            : "|cff66ccff[Report]|r Reported, thank you. Your location and target were included.");\n'
    b'                return;\n'
    b'            }\n'
    b'\n'
    b'            // A miss can also mean the readback raced ahead of the insert on a\n'
    b'            // multi-worker database pool. Look once more before declaring the\n'
    b'            // write lost: the retry lands a world tick later, by which time the\n'
    b'            // insert has certainly run.\n'
    b'            if (state->attempts++ < 1)\n'
    b'            {\n'
    b'                VerifyReportRow(state);\n'
    b'                return;\n'
    b'            }\n'
    b'\n'
    b'            LOG_ERROR("module.livinggear", "report DB write LOST from {} (account {}): {}",\n'
    b'                state->playerName, state->accountId, state->text);\n'
    b'            if (Player* player = ObjectAccessor::FindPlayer(state->playerGuid))\n'
    b'                ChatHandler(player->GetSession()).SendSysMessage(\n'
    b'                    "|cffff3333[Report]|r Your report could not be saved just now. "\n'
    b'                    "It was captured in the server log and will be recovered by hand -- no need to resend.");\n'
    b'        }));\n'
    b'}\n'
)
patch('modules/mod-living-gear/src/LivingGear_Support.cpp',
      b'std::string ExpandItemLinks(std::string text);\n',
      b'std::string ExpandItemLinks(std::string text);\n' + helpers)

print('patched: CharacterDatabase.h, CharacterDatabase.cpp, LivingGear_Support.cpp')
