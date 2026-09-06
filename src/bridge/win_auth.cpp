#include "win_auth.h"
#include <windows.h>
#include <wincred.h>
#include <string>
const std::wstring TARGET = L"ZhiAuth_Server_Token";
std::string to_utf8(const std::wstring& w) {
    if(w.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(sz-1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], sz, NULL, NULL);
    return s;
}
std::wstring to_utf16(const std::string& s) {
    if(s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(sz-1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], sz);
    return w;
}
extern "C" {
    int win_load_cred(char* user, char* pass) {
        PCREDENTIALW p = NULL;
        if (CredReadW(TARGET.c_str(), CRED_TYPE_GENERIC, 0, &p)) {
            strcpy(user, to_utf8(p->UserName).c_str());
            std::string pwd((char*)p->CredentialBlob, p->CredentialBlobSize);
            strcpy(pass, pwd.c_str());
            CredFree(p);
            return 1;
        }
        return 0;
    }
    int win_prompt_cred(char* user, char* pass, int* save) {
        CREDUI_INFOW cui = {0};
        cui.cbSize = sizeof(CREDUI_INFOW);
        cui.pszMessageText = L"Enter system credentials to bridge with ZhiAuth Server.";
        cui.pszCaptionText = L"ZhiAuth VFS Secure Login";
        WCHAR u[256] = {0}; WCHAR p[256] = {0}; BOOL s = FALSE;
        if (CredUIPromptForCredentialsW(&cui, TARGET.c_str(), NULL, 0, u, 256, p, 256, &s, CREDUI_FLAGS_GENERIC_CREDENTIALS | CREDUI_FLAGS_ALWAYS_SHOW_UI | CREDUI_FLAGS_SHOW_SAVE_CHECK_BOX | CREDUI_FLAGS_EXPECT_CONFIRMATION) == ERROR_SUCCESS) {
            strcpy(user, to_utf8(u).c_str());
            strcpy(pass, to_utf8(p).c_str());
            *save = s ? 1 : 0;
            SecureZeroMemory(u, sizeof(u)); SecureZeroMemory(p, sizeof(p));
            return 1;
        }
        return 0;
    }
    void win_save_cred(const char* user, const char* pass) {
        CREDENTIALW c = {0}; std::wstring wu = to_utf16(user); std::string pwd(pass);
        c.Type = CRED_TYPE_GENERIC; c.TargetName = (LPWSTR)TARGET.c_str();
        c.CredentialBlobSize = pwd.length(); c.CredentialBlob = (LPBYTE)pwd.c_str();
        c.Persist = CRED_PERSIST_LOCAL_MACHINE; c.UserName = (LPWSTR)wu.c_str();
        CredWriteW(&c, 0);
    }
    void win_delete_cred() { CredDeleteW(TARGET.c_str(), CRED_TYPE_GENERIC, 0); }
}
